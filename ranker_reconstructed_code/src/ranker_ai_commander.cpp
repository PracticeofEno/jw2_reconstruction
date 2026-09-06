#include "ranker_ai_commander.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace ranker {
namespace {
constexpr std::array<u32, 11> train_types{{0x20,0x21,0x22,0x24,0x25,0x27,0x28,0x29,0x2a,0x2c,0x2e}};
constexpr std::array<u32, 10> build_types{{0x80,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a}};
constexpr std::array<u32, 13> research_orders{{0x14,0x16,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x2a,0x2b,0x2d,0x38}};
constexpr u32 never = 0xffffffffu;
float norm(float value, float scale) { return std::clamp(value / scale, 0.0f, 1.0f); }
float log_resource(u32 value) { return std::clamp(std::log1p(float(value))/std::log1p(20000.0f),0.0f,1.0f); }
bool building(const AiObservedUnit& u) { return u.type_id >= 0x60 && u.type_id < 0xa0; }
bool worker(const AiObservedUnit& u) { return u.type_id < 0x40 && (u.type_id & 15) == 0; }
bool combat(const AiObservedUnit& u) { return u.type_id < 0x40 && !worker(u) && (u.type_flags & 0x20) != 0; }
bool hostile(const AiObservation& o, const AiObservedUnit& u) {
    return u.owner_id < 8 && u.owner_id != o.local_owner &&
        (o.local_relation_mask & (1u << u.owner_id)) == 0;
}
float weight(const AiObservedUnit& u) { return float(u.health) + u.attack_power + u.defense_power; }
float hp(const AiObservedUnit& u) { return u.max_health ? norm(float(u.health),float(u.max_health)) : 0; }
float speed(const AiObservedUnit& u) { return float(u.movement_step_limit)/std::max(1u,u.movement_period); }
CommanderPoint point(const AiObservedUnit& u) { return {u.x,u.y,true}; }
i64 distance2(CommanderPoint a, CommanderPoint b) { const i64 x=i64(a.x)-b.x,y=i64(a.y)-b.y; return x*x+y*y; }
float distance(CommanderPoint a, CommanderPoint b) { return std::sqrt(float(distance2(a,b))); }
bool near(CommanderPoint a, CommanderPoint b, i32 radius) { return a.valid && b.valid && distance2(a,b)<=i64(radius)*radius; }
std::size_t tile_index(const AiObservation& o, CommanderPoint p) {
    if (p.x<0||p.y<0||u32(p.x/32)>=o.map_width_tiles||u32(p.y/32)>=o.map_height_tiles) return o.tiles.size();
    return std::size_t(p.y/32)*o.map_width_tiles+u32(p.x/32);
}
bool visible_tile(const AiObservation& o, CommanderPoint p) { const auto i=tile_index(o,p); return i<o.tiles.size()&&o.tiles[i].visible; }
bool explored_tile(const AiObservation& o, CommanderPoint p) { const auto i=tile_index(o,p); return i<o.tiles.size()&&o.tiles[i].explored; }
float vision(const AiObservation& o, CommanderPoint p) { const auto i=tile_index(o,p); return i<o.tiles.size()?(o.tiles[i].visible?1.0f:o.tiles[i].explored?0.5f:0.0f):0.0f; }
const AiObservedUnit* find_unit(const std::vector<AiObservedUnit>& units,u32 id) {
    auto i=std::lower_bound(units.begin(),units.end(),id,[](const AiObservedUnit& u,u32 v){return u.id<v;});
    return i!=units.end()&&i->id==id?&*i:nullptr;
}
u32 type_index(const AiObservedUnit& u) { return (building(u)?16u:0u)+(u.type_id&15); }
bool busy(const AiObservedUnit& u) {
    // command_lockout_ticks is weapon recovery, when movement/kiting is
    // allowed. command_entry_lockout_ticks is the actual entry lock.
    const u32 state=u.command_state&0x00ffffffu;
    return u.command_entry_lockout_ticks||(u.command_state&0x50000000u)||
        u.type_flags==0x08002011u||state==0x24||state==0x45||state==0x5b||state==0x7e||
        (state>=0x5f&&state<=0x61)||state==0x6e||state==0x6f;
}
bool researching(const AiObservedUnit& u) {return (u.command_state&0xff)==0x4d||(u.command_state&0xff)==0x4e;}
u32 order_count(const AiObservedUnit& u,AiSemanticActionKind kind,u32 type) {
    u32 count=kind==AiSemanticActionKind::research?(researching(u)&&u.command_value==type):(u.queued_production_type_id==type);
    const u32 state=kind==AiSemanticActionKind::research?0x17:0x10;
    for(const auto& q:u.deferred_commands)if((q.state&0xff)==state&&u32(q.command_value_or_target)==type)++count;
    return count;
}
CommanderProductionInfo info(const CommanderServices& s,AiProductionRequestKind kind,u32 type) {
    if(s.production_info) return s.production_info(kind,type);
    CommanderProductionInfo r;
    if(kind==AiProductionRequestKind::unit) {
        constexpr u32 costs[16]={100,100,250,500,250,300,600,450,800,400,600,1500,5000,900,600,0};
        constexpr u32 pops[16]={1,1,1,2,2,1,2,2,4,2,4,8,25,4,3,0};
        r.cost=costs[type&15];r.population=pops[type&15];
    } else if(kind==AiProductionRequestKind::building) {
        constexpr u32 costs[16]={1000,0,200,350,400,600,600,400,500,800,800,0,0,0,0,0};
        r.cost=costs[type&15]; const auto f=AiBuildingFootprintOf(type);r.width=f.width;r.height=f.height;
    } else { r.cost=type==0x14?500:300; }
    return r;
}
bool valid(const CommanderServices& s,const AiSemanticAction& a) { return s.validator && s.validator(a); }
void diagnose(const CommanderServices& services,const char* event,u32 frame,
    const AiSemanticAction& action,const AiObservedUnit* source,u32 resources) {
    if(!services.diagnostic)return;
    std::ostringstream line;
    line<<"ai-commander: "<<event<<" frame="<<frame<<" source="<<(source?source->id:0)
        <<" kind="<<u32(action.kind)<<" production="<<action.production_id
        <<" site="<<action.target_x<<','<<action.target_y<<" resources="<<resources;
    if(source)line<<" state="<<source->command_state<<" flags="<<source->command_flags
        <<" xy="<<source->x<<','<<source->y<<" command="<<source->command_value
        <<" destination="<<source->destination_x<<','<<source->destination_y
        <<" path="<<source->path_target_x<<','<<source->path_target_y
        <<" queue="<<source->deferred_command_count<<" target="<<source->target_id;
    services.diagnostic(line.str());
}
AiSemanticAction order(AiSemanticActionKind kind,u32 id,CommanderPoint p={}) {
    AiSemanticAction a;a.kind=kind;a.unit_ids={id};a.target_x=p.x;a.target_y=p.y;return a;
}
bool same_order(const AiSemanticAction& a,const AiSemanticAction& b) {
    return a.kind==b.kind&&a.target_unit_id==b.target_unit_id&&a.target_x==b.target_x&&a.target_y==b.target_y&&
        a.production_id==b.production_id&&a.stance_id==b.stance_id&&a.stance_on==b.stance_on&&a.queued==b.queued;
}
CommanderPoint clamp_point(const AiObservation& o,CommanderPoint p) {
    p.x=std::clamp(p.x,0,std::max(0,i32(o.map_width_tiles)*32-1));
    p.y=std::clamp(p.y,0,std::max(0,i32(o.map_height_tiles)*32-1));return p;
}
CommanderPoint ground_point(const AiObservation& o,CommanderPoint p) {
    p=clamp_point(o,p);const auto t=tile_index(o,p);
    if(t<o.tiles.size()&&o.tiles[t].passable) return p;
    for(i32 r=1;r<=8;++r) for(i32 y=-r;y<=r;++y) for(i32 x=-r;x<=r;++x) {
        if(std::max(std::abs(x),std::abs(y))!=r) continue;
        CommanderPoint q{(p.x/32+x)*32+16,(p.y/32+y)*32+16,true}; const auto i=tile_index(o,q);
        if(i<o.tiles.size()&&o.tiles[i].passable) return q;
    }
    return p;
}
std::vector<CommanderPoint> path(const AiObservation& o,CommanderPoint from,CommanderPoint to) {
    from=ground_point(o,from);to=ground_point(o,to);
    const auto a=tile_index(o,from),b=tile_index(o,to);if(a>=o.tiles.size()||b>=o.tiles.size()) return {};
    std::vector<i32> parent(o.tiles.size(),-1),todo;todo.reserve(o.tiles.size());todo.push_back(i32(a));parent[a]=i32(a);
    constexpr i32 dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
    for(std::size_t h=0;h<todo.size()&&parent[b]<0;++h) {
        const i32 cur=todo[h],cx=cur%i32(o.map_width_tiles),cy=cur/i32(o.map_width_tiles);
        for(u32 k=0;k<4;++k) {
            const i32 x=cx+dx[k],y=cy+dy[k];if(x<0||y<0||x>=i32(o.map_width_tiles)||y>=i32(o.map_height_tiles))continue;
            const auto n=std::size_t(y)*o.map_width_tiles+x;if(parent[n]>=0||!o.tiles[n].passable)continue;
            parent[n]=cur;todo.push_back(i32(n));
        }
    }
    if(parent[b]<0)return {};
    std::vector<CommanderPoint> result;
    for(i32 cur=i32(b);;cur=parent[cur]) {
        result.push_back({(cur%i32(o.map_width_tiles))*32+16,(cur/i32(o.map_width_tiles))*32+16,true});if(cur==i32(a))break;
    }
    std::reverse(result.begin(),result.end());return result;
}
bool completed(const CommanderView& v,u32 type) { return v.own_counts[16+(type&15)]!=0; }
bool build_prerequisite(const CommanderView& v,u32 type) {
    switch(type) {
    case 0x80:case 0x82:return true;
    case 0x84:return completed(v,0x80);
    case 0x83:case 0x85:case 0x86:case 0x87:return completed(v,0x84);
    case 0x88:return completed(v,0x85)||completed(v,0x86);
    case 0x89:return completed(v,0x85)&&completed(v,0x88);
    case 0x8a:return completed(v,0x86)&&completed(v,0x88);
    default:return false;
    }
}
bool merge_material(const AiObservedUnit& u) {
    return combat(u)&&u.command_state==1&&hp(u)>=0.9f&&!(u.command_flags&0x10)&&!busy(u);
}
bool construction_worker(const CommanderState& s,u32 id) {
    return std::any_of(s.builds.begin(),s.builds.end(),[id](const CommanderBuildReservation& r){return !r.order.unit_ids.empty()&&r.order.unit_ids[0]==id;});
}
bool matching_identity(const CommanderState& s,u32 id,u32 generation) {
    const auto found=s.units.find(id);return found!=s.units.end()&&found->second.generation==generation;
}
u32 queued_population(const AiObservation& o,const CommanderServices& s,std::array<u32,32>& pending) {
    u32 result=0;
    for(const auto& u:o.units)if(u.controlled&&u.alive) {
        if(u.under_construction&&building(u))++pending[type_index(u)];
        if(!building(u))continue;
        if(u.queued_production_type_id>0&&u.queued_production_type_id<0x40)++pending[u.queued_production_type_id&15];
        for(const auto& q:u.deferred_commands) {
            // Building production queues encode the type in command_value.
            const u32 t=u32(q.command_value_or_target);
            if((q.state&0xff)!=0x10||t>=0x40)continue;
            result+=info(s,AiProductionRequestKind::unit,t).population;++pending[t&15];
        }
    }
    return result;
}

void refresh_build_components(CommanderState& s,const AiObservation& o,const std::vector<u8>& occupancy) {
    std::vector<u8> open(o.tiles.size());
    for(std::size_t i=0;i<open.size();++i)open[i]=o.tiles[i].passable&&(i>=occupancy.size()||!occupancy[i]);
    if(open==s.build_open_cells&&s.build_component_width==o.map_width_tiles)return;
    s.build_component_width=o.map_width_tiles;
    s.build_open_cells=std::move(open);s.build_components.assign(o.tiles.size(),0);
    std::vector<u32> queue;queue.reserve(o.tiles.size());u32 component=0;
    constexpr i32 dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
    for(u32 start=0;start<s.build_open_cells.size();++start)if(s.build_open_cells[start]&&!s.build_components[start]) {
        queue.clear();queue.push_back(start);s.build_components[start]=++component;
        for(std::size_t cursor=0;cursor<queue.size();++cursor) {
            const u32 current=queue[cursor];const i32 cx=i32(current%o.map_width_tiles),cy=i32(current/o.map_width_tiles);
            for(u32 d=0;d<4;++d) {
                const i32 x=cx+dx[d],y=cy+dy[d];
                if(x<0||y<0||x>=i32(o.map_width_tiles)||y>=i32(o.map_height_tiles))continue;
                const auto next=std::size_t(y)*o.map_width_tiles+x;
                if(s.build_open_cells[next]&&!s.build_components[next]){s.build_components[next]=component;queue.push_back(u32(next));}
            }
        }
    }
}
bool builder_reaches_site(const CommanderState& s,const AiObservation& o,const AiObservedUnit& worker,CommanderPoint site) {
    const auto target=tile_index(o,site);if(target>=s.build_components.size()||!s.build_components[target])return false;
    const u32 component=s.build_components[target];const auto source=tile_index(o,point(worker));
    if(source>=s.build_components.size())return false;
    if(s.build_components[source])return s.build_components[source]==component;
    // Workers may stand on a footprint's edge after dropping cargo. Permit
    // only an adjacent exit; projecting several tiles can jump an enclosure.
    constexpr i32 dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
    for(u32 d=0;d<4;++d) {
        const auto next=tile_index(o,{(worker.x/32+dx[d])*32,(worker.y/32+dy[d])*32,true});
        if(next<s.build_components.size()&&s.build_components[next]==component)return true;
    }
    return false;
}
std::vector<AiSemanticAction> build_plan(const CommanderState& s,const AiObservation& o,
    const CommanderView& v,u32 type,CommanderPoint center,const std::vector<u8>& occupancy,
    const CommanderBuildReservation* replacing=nullptr,i32 radius=12) {
    if(!center.valid||!build_prerequisite(v,type))return {};
    const auto cost=info(v.services,AiProductionRequestKind::building,type).cost;
    const u32 reserved=v.reserved_resources-(replacing?std::min(v.reserved_resources,replacing->cost):0u);
    if(u64(cost)+reserved>o.primary_resources)return {};
    const u32 index=16+(type&15),limit=type==0x80||type==0x84?3:type==0x83?6:type==0x82?24:1;
    const u32 pending=v.pending_counts[index]-(replacing&&v.pending_counts[index]?1u:0u);
    if(v.own_counts[index]+pending>=limit||pending>=(type==0x82?2u:1u))return {};
    if(type==0x82&&o.population_used>=180)return {};
    const u32 retry_worker=replacing&&!replacing->order.unit_ids.empty()?replacing->order.unit_ids[0]:0;
    std::vector<const AiObservedUnit*> workers;
    for(const auto& u:v.own)if(worker(u)&&!busy(u)&&(!construction_worker(s,u.id)||u.id==retry_worker)&&u.id!=s.scout_id)workers.push_back(&u);
    if(workers.empty())return {};
    const auto footprint=AiBuildingFootprintOf(type);
    // Candidate ordering is deterministic Chebyshev spiral, then y/x.
    for(i32 r=0;r<=radius;++r)for(i32 y=-r;y<=r;++y)for(i32 x=-r;x<=r;++x) {
        if(std::max(std::abs(x),std::abs(y))!=r)continue;
        const i32 tx=center.x/32+x,ty=center.y/32+y;bool blocked=false,ring=false;
        if(!AiBuildSiteCandidateOk(o,occupancy,type,tx,ty,true,&blocked,{},&ring)||blocked||!ring)continue;
        bool overlap=false;
        for(const auto& b:s.builds) {
            if(&b==replacing)continue;
            const auto f=AiBuildingFootprintOf(b.order.production_id);
            const i32 bx=b.order.target_x/32,by=b.order.target_y/32;
            if(tx<=bx+i32(f.width)&&tx+i32(footprint.width)>=bx&&ty<=by+i32(f.height)&&ty+i32(footprint.height)>=by){overlap=true;break;}
        }
        if(overlap)continue;
        bool berry_close=false;
        for(i32 by=ty-2;by<ty+i32(footprint.height)+2&&!berry_close;++by)for(i32 bx=tx-2;bx<tx+i32(footprint.width)+2;++bx) {
            const auto t=tile_index(o,{bx*32,by*32,true});if(t<o.tiles.size()&&o.tiles[t].resource_amount){berry_close=true;break;}
        }
        // The required fully open one-tile ring already connects every
        // boundary cell. Only HQ placement needs the additional berry path.
        if(berry_close||(type==0x80&&!AiBuildSiteKeepsLocalPaths(o,occupancy,type,tx,ty,true)))continue;
        const CommanderPoint site{tx*32,ty*32,true};
        std::sort(workers.begin(),workers.end(),[site,retry_worker](auto a,auto b){return std::make_tuple(a->id==retry_worker,distance2(point(*a),site),a->id)<std::make_tuple(b->id==retry_worker,distance2(point(*b),site),b->id);});
        for(const auto* w:workers) {
            if(!builder_reaches_site(s,o,*w,site))continue;
            auto a=order(AiSemanticActionKind::build,w->id,site);a.production_id=type;
            if(valid(v.services,a))return {a};
        }
    }
    return {};
}

bool intent_anchor(const CommanderView& v,u32 intent,u32 a) {
    if(a>=16||!v.anchors[a].valid)return false;
    switch(CommanderIntent(intent)) {
    case CommanderIntent::hold:case CommanderIntent::retreat:return a<=3;
    case CommanderIntent::defend:return a==10;
    case CommanderIntent::hunt:return a==11;
    case CommanderIntent::scout:return a==12||a==4||a==9||a==13;
    case CommanderIntent::harass:return a==8||a==9;
    case CommanderIntent::siege:return a==4||a==6||a==9;
    case CommanderIntent::attack_move:return a!=11;
    }
    return false;
}
void fill_masks(CommanderView& v) {
    v.mask.fill(0);v.mask[0]=1;
    for(std::size_t i=1;i<42;++i)v.mask[i]=!v.macro_plans[i].empty();
    // Transfers are registry operations without engine packets.
    v.mask[38]=!v.squads[0].members.empty();v.mask[39]=!v.squads[1].members.empty();
    v.mask[40]=!v.squads[0].members.empty();v.mask[41]=!v.squads[2].members.empty();
    for(u32 i=0;i<16;++i)v.mask[42+i]=v.anchors[i].valid;
    v.mask[58]=1;for(u32 i=0;i<3;++i)v.mask[59+i]=!v.squads[i].members.empty();
    for(u32 i=0;i<8;++i)for(u32 a=0;a<16;++a)if(intent_anchor(v,i,a))v.mask[62+i]=1;
    for(u32 a=0;a<16;++a)v.mask[70+a]=v.anchors[a].valid;
    for(u32 i=86;i<95;++i)v.mask[i]=1;
    if(!v.near_enemies)v.mask[90]=v.mask[91]=0;
}

} // namespace

CommanderView BuildCommanderView(CommanderState& s,const AiObservation& o,const CommanderServices& services) {
    CommanderView v;v.frame=o.simulation_frame;v.services=services;
    const u32 f=v.frame;bool spawn=false,damage=s.external_damage_pending,new_enemy=false;
    std::vector<u32> damaged_assets;
    if(damage)damaged_assets.push_back(s.external_damage_unit_id);
    s.external_damage_pending=false;
    // Never consult hidden entries, even if a caller supplies an unsanitized
    // unit vector. Slot replacement is learned only from a visible sighting.
    for(const auto& u:o.units)if(u.alive&&(u.controlled||u.visible)) {
        if(u.controlled)v.own.push_back(u);
        else if(hostile(o,u))v.visible_enemies.push_back(u);
        else if(u.owner_id==8)v.visible_neutrals.push_back(u);
    }
    auto sort_ids=[](auto& units){std::sort(units.begin(),units.end(),[](const auto& a,const auto& b){return a.id<b.id;});};
    sort_ids(v.own);sort_ids(v.visible_enemies);sort_ids(v.visible_neutrals);
    for(auto& g:s.ghosts)g.second.visible_now=false;
    auto see=[&](const AiObservedUnit& u) {
        auto previous=s.ghosts.find(u.id);
        if(combat(u)&&(previous==s.ghosts.end()||f-previous->second.last_seen_frame>8)) {
            for(const auto& asset:v.own)if((building(asset)||worker(asset))&&near(point(asset),point(u),384)){new_enemy=true;break;}
        }
        if(u.runtime_slot_index!=kInvalidUnitRuntimeSlotIndex)for(auto it=s.ghosts.begin();it!=s.ghosts.end();) {
            if(it->first!=u.id&&it->second.seen.runtime_slot_index==u.runtime_slot_index)it=s.ghosts.erase(it);else ++it;
        }
        s.ghosts[u.id]={u,f,true,services.generation_of?services.generation_of(u.id):0};
    };
    for(const auto& u:v.visible_enemies)see(u);
    for(const auto& u:v.visible_neutrals)see(u);
    for(auto it=s.ghosts.begin();it!=s.ghosts.end();) {
        const auto& g=it->second;
        if(!g.visible_now&&(visible_tile(o,point(g.seen))||(!building(g.seen)&&f-g.last_seen_frame>=2200)))it=s.ghosts.erase(it);
        else { if(g.seen.owner_id==8)v.neutrals.push_back(g);else v.enemies.push_back(g);++it; }
    }
    for(const auto& u:v.own) {
        auto it=s.units.find(u.id);
        const u32 generation=services.generation_of?services.generation_of(u.id):0;
        if(it==s.units.end()||it->second.slot!=u.runtime_slot_index||it->second.type!=u.type_id||it->second.generation!=generation) {
            CommanderUnitState state;state.slot=u.runtime_slot_index;state.type=u.type_id;state.squad=worker(u)?3:s.rally_squad;
            state.generation=generation;
            state.investment=info(services,building(u)?AiProductionRequestKind::building:AiProductionRequestKind::unit,u.type_id).cost;
            for(const auto& m:s.merges)if(m.issued&&f-m.started_frame<600&&near(point(u),m.center,128)&&
                (u.type_id==0x23||u.type_id==0x26||u.type_id==0x2d||u.type_id==0x2b)) {
                state.squad=m.squad;state.investment=m.investment;break;
            }
            state.x=u.x;state.y=u.y;state.health=u.health;state.born_frame=f;state.last_progress=f;state.completed=!u.under_construction;
            s.units[u.id]=state;spawn=s.initialized;it=s.units.find(u.id);
        }
        auto& state=it->second;
        if(!state.completed&&!u.under_construction){spawn=true;state.completed=true;}
        if(u.health<state.health&&(building(u)||worker(u))) {
            damage=true;s.threat=point(u);s.threat_frame=f;s.damage_frames.push_back(f);
            damaged_assets.push_back(building(u)?u.id:0);
        }
        if(std::abs(u.x-state.x)+std::abs(u.y-state.y)>=8)state.last_progress=f;
        state.x=u.x;state.y=u.y;state.health=u.health;state.last_seen=f;
        if(worker(u))++v.workers;
        if(!u.under_construction&&u.type_id<0xa0)++v.own_counts[type_index(u)];
        if(combat(u)&&!u.under_construction) {
            ++v.army_count;v.own_weight+=weight(u);auto& squad=v.squads[std::min<u8>(state.squad,2)];
            squad.members.push_back(u.id);squad.weight+=weight(u);squad.investment+=state.investment;
            squad.center.x+=u.x;squad.center.y+=u.y;
        }
    }
    for(auto it=s.units.begin();it!=s.units.end();)if(it->second.last_seen!=f)it=s.units.erase(it);else ++it;
    s.merges.erase(std::remove_if(s.merges.begin(),s.merges.end(),[&](const CommanderMergeReservation& m) {
        if(!m.issued) {
            if(m.generations.size()!=m.units.size())return true;
            for(std::size_t i=0;i<m.units.size();++i)if(!matching_identity(s,m.units[i],m.generations[i]))return true;
            return false;
        }
        for(const auto& u:v.own)if((u.type_id==0x23||u.type_id==0x26||u.type_id==0x2d||u.type_id==0x2b)&&
            s.units.at(u.id).born_frame>m.started_frame&&near(point(u),m.center,128))return true;
        return false;
    }),s.merges.end());
    for(auto it=s.receipts.begin();it!=s.receipts.end();) {
        if(f<=it->frame){++it;continue;}
        const auto* u=it->order.unit_ids.empty()?nullptr:find_unit(v.own,it->order.unit_ids[0]);
        if(u&&!matching_identity(s,u->id,it->source_generation))u=nullptr;
        bool applied=u&&order_count(*u,it->order.kind,it->order.production_id)>it->before_count;
        if(it->order.kind==AiSemanticActionKind::produce_unit)for(const auto& owned:s.units)
            if(owned.second.type==it->order.production_id&&owned.second.born_frame>it->frame)applied=true;
        if(it->order.kind==AiSemanticActionKind::research&&it->order.production_id<64)
            applied|=o.research_order_levels[it->order.production_id]>it->before_level;
        if(!applied&&u){++s.silent_rejections;diagnose(services,"receipt-failed",f,it->order,u,o.primary_resources);}
        it=s.receipts.erase(it);
    }
    for(u32 i=0;i<3;++i) {
        auto& q=v.squads[i];q.intent=s.squads[i].intent;q.roe=s.squads[i].roe;q.anchor=s.squads[i].anchor;
        if(!q.members.empty()){q.center.x/=i32(q.members.size());q.center.y/=i32(q.members.size());q.center.valid=true;}
    }
    s.damage_frames.erase(std::remove_if(s.damage_frames.begin(),s.damage_frames.end(),[f](u32 d){return f-d>128;}),s.damage_frames.end());
    if(s.initialized&&s.last_research!=o.research_order_levels)spawn=true;
    s.last_research=o.research_order_levels;
    s.income_history.push_back({f,services.cumulative_gathered});
    while(s.income_history.size()>1&&f-s.income_history[1].first>=220)s.income_history.erase(s.income_history.begin());
    if(!s.income_history.empty())v.income_rate=float(services.cumulative_gathered-s.income_history.front().second);
    v.queued_population=queued_population(o,services,v.pending_counts);
    for(auto it=s.builds.begin();it!=s.builds.end();) {
        const auto* w=it->order.unit_ids.empty()?nullptr:find_unit(v.own,it->order.unit_ids[0]);
        bool created=false;
        for(const auto& u:v.own)if(u.type_id==it->order.production_id&&s.units.at(u.id).born_frame>it->issued_frame&&
            near(point(u),{it->order.target_x,it->order.target_y,true},192)){created=true;break;}
        if(created||!w||!matching_identity(s,w->id,it->source_generation)) {
            diagnose(services,created?"build-created":"build-worker-lost",f,it->order,w,o.primary_resources);
            it=s.builds.erase(it);continue;
        }
        it->acknowledged=AiWalkingBuildTypeOf(*w)==it->order.production_id;
        if(f-it->issued_frame>16&&!it->acknowledged&&it->attempts>=3){++s.silent_rejections;diagnose(services,"build-expired",f,it->order,w,o.primary_resources);s.units.at(w->id).post_build_harvest={};it=s.builds.erase(it);continue;}
        v.reserved_resources+=it->cost;++v.pending_counts[16+(it->order.production_id&15)];++it;
    }
    std::vector<const AiObservedUnit*> bases;
    for(const auto& u:v.own)if(u.type_id==0x80&&!u.under_construction)bases.push_back(&u);
    v.anchors[0]=bases.empty()?CommanderPoint{o.start_x,o.start_y,true}:point(*bases.front());
    std::array<u32,2> berry_amount{};std::set<std::size_t> base_tiles;
    for(std::size_t b=0;b<bases.size();++b) {
        u32 tiles=0;const auto p=point(*bases[b]);
        for(i32 y=p.y/32-15;y<=p.y/32+15;++y)for(i32 x=p.x/32-15;x<=p.x/32+15;++x) {
            const auto t=tile_index(o,{x*32,y*32,true});if(t>=o.tiles.size()||!o.tiles[t].resource_amount)continue;
            if(b<2)berry_amount[b]+=o.tiles[t].resource_amount;
            if(base_tiles.insert(t).second)++tiles;
        }
        v.worker_cap+=tiles*5/2;
    }
    if(!s.expansion_initialized) {
        // Public terrain topology and berry locations do not change. Run the
        // expensive footprint/connectivity search once, without dynamic
        // occupants. Exact live occupancy is checked when planning a build.
        AiObservation terrain=o;terrain.units.clear();
        s.expansion=ComputeAiExpansionPlan(terrain);
        std::vector<u8> seen(o.tiles.size(),0);
        for(u32 seed=0;seed<o.tiles.size();++seed)if(!seen[seed]&&o.tiles[seed].resource_amount) {
            s.expansion_members.push_back({seed});seen[seed]=1;
            auto& members=s.expansion_members.back();
            for(std::size_t next=0;next<members.size();++next) {
                const i32 x=i32(members[next]%o.map_width_tiles),y=i32(members[next]/o.map_width_tiles);
                // Match ComputeAiExpansionPlan's public gap-three components
                // and row-major component order exactly.
                for(i32 dy=-3;dy<=3;++dy)for(i32 dx=-3;dx<=3;++dx) {
                    const auto t=tile_index(o,{(x+dx)*32,(y+dy)*32,true});
                    if(t<o.tiles.size()&&!seen[t]&&o.tiles[t].resource_amount){seen[t]=1;members.push_back(u32(t));}
                }
            }
        }
        s.expansion_initialized=true;s.expansion_frame=f;
    }
    // Depletion, exploration and ownership are refreshed every executor tick,
    // rather than staying stale until the previous 256-frame full rescan.
    s.expansion.has_target=false;s.expansion.nest_count=0;s.expansion.nest_walkers=0;
    std::vector<CommanderPoint> developed;
    for(const auto& u:v.own) {
        if(u.type_id==0x80){developed.push_back(point(u));++s.expansion.nest_count;}
        else if(AiWalkingBuildTypeOf(u)==0x80){developed.push_back({u.path_target_x,u.path_target_y,true});++s.expansion.nest_count;++s.expansion.nest_walkers;}
    }
    for(const auto& b:s.builds)if(b.order.production_id==0x80)developed.push_back({b.order.target_x,b.order.target_y,true});
    for(std::size_t i=0;i<s.expansion.clusters.size();++i) {
        auto& c=s.expansion.clusters[i];c.known_amount=0;c.tile_count=0;
        if(i<s.expansion_members.size())for(u32 t:s.expansion_members[i]) {
            c.known_amount+=o.tiles[t].resource_amount;c.tile_count+=o.tiles[t].resource_amount>0;
        }
        const CommanderPoint site{c.site_x,c.site_y,c.site_x>=0};
        c.site_explored=site.valid&&explored_tile(o,site);c.developed=false;c.site_blocked=false;
        for(const auto p:developed)if(near(p,site,512)){c.developed=true;break;}
        const auto footprint=AiBuildingFootprintOf(0x80);
        auto blocks=[&](const AiObservedUnit& u) {
            return u.x/32>=c.site_x/32&&u.x/32<c.site_x/32+i32(footprint.width)&&
                u.y/32>=c.site_y/32&&u.y/32<c.site_y/32+i32(footprint.height);
        };
        for(const auto& u:v.own)c.site_blocked|=blocks(u);
        for(const auto& u:v.visible_enemies)c.site_blocked|=blocks(u);
        for(const auto& u:v.visible_neutrals)c.site_blocked|=blocks(u);
        if(site.valid&&!c.developed&&c.known_amount&&(!s.expansion.has_target||
            c.site_distance_from_start_sq<s.expansion.clusters[s.expansion.target_index].site_distance_from_start_sq)) {
            s.expansion.has_target=true;s.expansion.target_index=i;
        }
    }
    if(s.expansion.has_target) {
        const auto& c=s.expansion.clusters[s.expansion.target_index];
        s.expansion.target_x=c.site_x;s.expansion.target_y=c.site_y;
        s.expansion.target_explored=c.site_explored;s.expansion.target_blocked=c.site_blocked;
    }
    if(bases.size()>1)v.anchors[3]=point(*bases[1]);
    else if(s.expansion.has_target)v.anchors[3]={s.expansion.target_x,s.expansion.target_y,true};
    const auto main=v.squads[0].center.valid?v.squads[0].center:v.anchors[0];
    float nearest_start=std::numeric_limits<float>::max();u32 unexplored=0,enemy_bases=0;
    for(u32 i=0;i<8;++i)if(o.start_candidate_mask&(1u<<i)) {
        CommanderPoint p{o.start_candidate_x[i],o.start_candidate_y[i],true};
        if(near(p,{o.start_x,o.start_y,true},160)||explored_tile(o,p))continue;
        ++unexplored;
        if(s.start_path_lengths[i]==0) {
            const auto route=path(o,v.anchors[0],p);
            s.start_path_lengths[i]=route.empty()?never:u32(route.size());
        }
        if(s.start_path_lengths[i]!=never&&s.start_path_lengths[i]<nearest_start){nearest_start=float(s.start_path_lengths[i]);v.anchors[12]=p;}
    }
    // Sweep: once every start slot is explored, anchor 12 rotates through
    // the berry cluster sites we have seen least recently (excluding our own
    // bases), so a hidden last building or expansion is found and finished.
    if(s.cluster_last_visible.size()!=s.expansion.clusters.size())s.cluster_last_visible.assign(s.expansion.clusters.size(),0);
    for(std::size_t i=0;i<s.expansion.clusters.size();++i) {
        const auto& c=s.expansion.clusters[i];if(c.site_x<0)continue;
        if(visible_tile(o,{c.site_x,c.site_y,true}))s.cluster_last_visible[i]=f;
    }
    if(!unexplored) {
        u32 oldest=never;CommanderPoint sweep;
        for(std::size_t i=0;i<s.expansion.clusters.size();++i) {
            const auto& c=s.expansion.clusters[i];if(c.site_x<0)continue;
            const CommanderPoint p{c.site_x,c.site_y,true};
            if(near(p,v.anchors[0],640)||(v.anchors[3].valid&&near(p,v.anchors[3],640)))continue;
            if(s.cluster_last_visible[i]<oldest){oldest=s.cluster_last_visible[i];sweep=p;}
        }
        if(sweep.valid)v.anchors[12]=sweep;
    }
    float nearest_building=std::numeric_limits<float>::max();i64 ex=0,ey=0,wx=0,wy=0;u32 ec=0,wc=0,visible_count=0;float mem_weight=0;
    CommanderPoint confirmed_enemy;
    for(const auto& g:v.enemies) {
        const auto& u=g.seen;
        if(building(u)) {
            const auto d=distance(main,point(u));if(d<nearest_building){nearest_building=d;v.anchors[6]=point(u);}
            if((u.type_id&15)==0){++enemy_bases;if(!confirmed_enemy.valid)confirmed_enemy=point(u);else v.anchors[9]=point(u);}
        }
        if(worker(u)){wx+=u.x;wy+=u.y;++wc;}
        if(combat(u)) {
            mem_weight+=weight(u)*std::pow(0.97f,float(f-g.last_seen_frame)/64.0f);
            if(g.visible_now){ex+=u.x;ey+=u.y;++ec;++visible_count;v.enemy_weight+=weight(u);s.last_army_seen=f;}
        }
    }
    v.anchors[4]=confirmed_enemy.valid?confirmed_enemy:v.anchors[12];
    if(ec)v.anchors[7]={i32(ex/ec),i32(ey/ec),true};
    if(wc)v.anchors[8]={i32(wx/wc),i32(wy/wc),true};
    if(s.threat.valid&&f-s.threat_frame<=220)v.anchors[10]=s.threat;
    if(v.squads[0].center.valid)v.anchors[14]=v.squads[0].center;
    if(v.anchors[4].valid) {
        if(!s.route_destination.valid||distance2(s.route_destination,v.anchors[4])>1024) {
            s.route=path(o,v.anchors[0],v.anchors[4]);s.route_destination=v.anchors[4];
        }
        auto route_at=[&](float fraction) {
            if(services.route_point)return services.route_point(v.anchors[0],v.anchors[4],fraction);
            if(s.route.empty())return CommanderPoint{};
            return s.route[std::min(s.route.size()-1,std::size_t(fraction*(s.route.size()-1)))];
        };
        v.anchors[1]=route_at(s.route.empty()?0.15f:std::min(0.3f,9.0f/float(s.route.size())));
        v.anchors[2]=route_at(0.15f);v.anchors[5]=route_at(0.3f);v.anchors[15]=route_at(0.5f);
        // Python Jurassic's public plateau exits. Other maps use route15%.
        if(o.map_width_tiles==128&&o.map_height_tiles==128&&o.map_relative_path.find("Python")!=std::string::npos) {
            if(o.start_y<640&&o.start_x<2600){v.anchors[2]={54*32+16,6*32+16,true};v.closed_plateau=true;}
            else if(o.start_y<2600&&o.start_x>3200){v.anchors[2]={116*32+16,65*32+16,true};v.closed_plateau=true;}
            if(v.closed_plateau) {
                // Hold at the top of the ramp, a few tiles inside the plateau
                // toward the HQ, not inside the narrow ramp itself; the tower
                // spiral around this point still covers the ramp (range 330).
                const float d=std::max(1.0f,distance(v.anchors[2],v.anchors[0]));
                v.anchors[2].x+=i32((v.anchors[0].x-v.anchors[2].x)*160/d);
                v.anchors[2].y+=i32((v.anchors[0].y-v.anchors[2].y)*160/d);
                v.anchors[2]=ground_point(o,v.anchors[2]);
            }
        }
    }
    if(!v.anchors[1].valid)v.anchors[1]=v.anchors[0];
    if(!v.anchors[2].valid)v.anchors[2]=v.anchors[1];
    float hunt_distance=std::numeric_limits<float>::max();
    for(const auto& g:v.neutrals)if(g.seen.attack_power==0) {
        const float d=distance(main,point(g.seen));if(d<hunt_distance){hunt_distance=d;v.anchors[11]=point(g.seen);}
    }
    float contested=std::numeric_limits<float>::max(),enemy_expansion=std::numeric_limits<float>::max();
    for(const auto& c:s.expansion.clusters)if(!c.developed&&c.site_x>=0) {
        CommanderPoint p{c.site_x,c.site_y,true};const auto d=distance(v.anchors[0],p);
        if(d<contested){contested=d;v.anchors[13]=p;}
        if(confirmed_enemy.valid&&distance(confirmed_enemy,p)<enemy_expansion&&distance(confirmed_enemy,p)>512){enemy_expansion=distance(confirmed_enemy,p);if(enemy_bases<2)v.anchors[9]=p;}
    }
    bool near_army=false;
    for(const auto& enemy:v.visible_enemies)for(const auto& u:v.own)if(building(u)&&near(point(u),point(enemy),400)) {
        ++v.near_enemies;if(combat(enemy))near_army=true;break;
    }
    if(near_army&&!s.army_near_before){++s.waves_seen;s.last_wave_frame=f;}s.army_near_before=near_army;
    s.max_enemy_count=std::max(s.max_enemy_count,visible_count);

    // Macro plans share authoritative semantic validation with execution.
    const u32 available=o.primary_resources>v.reserved_resources?o.primary_resources-v.reserved_resources:0;
    for(u32 i=0;i<train_types.size();++i) {
        const u32 t=train_types[i];const auto cost=info(services,AiProductionRequestKind::unit,t);
        if(cost.cost>available||o.population_reserved+v.queued_population+cost.population>std::min(180u,o.population_used))continue;
        if(t==0x20&&v.workers+v.pending_counts[0]>=v.worker_cap)continue;
        if(t==0x2c&&(v.own_counts[12]||v.pending_counts[12]))continue;
        if(services.curriculum_stage<3&&(t==0x29||t==0x2c))continue;
        const u32 producer=t==0x20||t==0x2c?0x80:t==0x29||t==0x2a?0x87:0x84;
        std::vector<const AiObservedUnit*> producers;
        for(const auto& u:v.own)if(u.type_id==producer&&!u.under_construction&&u.deferred_command_count<4)producers.push_back(&u);
        std::sort(producers.begin(),producers.end(),[](auto a,auto b){return std::make_tuple(a->deferred_command_count,a->queued_production_type_id!=0,a->id)<std::make_tuple(b->deferred_command_count,b->queued_production_type_id!=0,b->id);});
        for(const auto* u:producers) {auto a=order(AiSemanticActionKind::produce_unit,u->id);a.production_id=t;if(valid(services,a)){v.macro_plans[1+i]={a};break;}}
    }
    AiObservation placement_observation=o;
    placement_observation.units=v.own;
    placement_observation.units.insert(placement_observation.units.end(),v.visible_enemies.begin(),v.visible_enemies.end());
    placement_observation.units.insert(placement_observation.units.end(),v.visible_neutrals.begin(),v.visible_neutrals.end());
    v.build_occupancy=AiBuildOccupancyGrid(placement_observation);
    const auto& occupancy=v.build_occupancy;
    refresh_build_components(s,placement_observation,occupancy);
    std::map<std::pair<i32,i32>,bool> reachable_expansion;
    // Placement search (spiral candidates x footprint gates x path checks for
    // up to 16 anchors and 10 types) is the dominant view cost. Run it in
    // full on fixed decision frames only; other ticks reuse cached plans
    // whose worker and site are re-validated by the engine planner.
    auto revalidate=[&](std::vector<AiSemanticAction> plan) {
        if(plan.empty()||plan[0].unit_ids.empty())return std::vector<AiSemanticAction>{};
        const auto* w=find_unit(v.own,plan[0].unit_ids[0]);
        if(!w||busy(*w)||construction_worker(s,w->id)||w->id==s.scout_id||!valid(services,plan[0]))return std::vector<AiSemanticAction>{};
        return plan;
    };
    // Cache key: workers not tied up in construction/scouting. Momentary
    // harvest arrival locks are excluded so the key does not flap each tick.
    u32 available_workers=0,terrain_key=0;
    for(const auto& u:v.own)if(worker(u)&&!construction_worker(s,u.id)&&u.id!=s.scout_id) {
        const u32 state=u.command_state&0x00ffffffu;
        if(state!=0x24&&state!=0x5b&&state!=0x7e&&!(u.command_state&0x50000000u))++available_workers;
    }
    for(std::size_t i=0;i<o.tiles.size();++i)if(!o.tiles[i].passable)terrain_key=terrain_key*31u+u32(i)+1u;
    bool full_plan_search=!s.plan_cache_valid||(f>=1&&(f-1)%32==0)||f-s.plan_cache_frame>=32||terrain_key!=s.plan_cache_terrain;
    if(!full_plan_search) {
        // The cache is only trusted while the world it was searched in holds:
        // the same free-worker count, no building cost newly affordable, and
        // every cached worker/site still usable. Otherwise search again now
        // rather than lose the action until the next decision frame.
        if(available_workers!=s.plan_cache_workers)full_plan_search=true;
        for(u32 type:build_types) {
            const u32 cost=info(services,AiProductionRequestKind::building,type).cost;
            if(s.plan_cache_available<cost&&available>=cost)full_plan_search=true;
        }
        for(const auto& p:s.cached_hq_plans)if(!p.empty()&&revalidate(p).empty())full_plan_search=true;
        for(const auto& p:s.cached_tower_plans)if(!p.empty()&&revalidate(p).empty())full_plan_search=true;
        for(const auto& p:s.cached_build_plans)if(!p.empty()&&revalidate(p).empty())full_plan_search=true;
    }
    for(u32 i=0;i<build_types.size();++i) {
        const u32 type=build_types[i];if(services.curriculum_stage<3&&type==0x8a)continue;
        if(info(services,AiProductionRequestKind::building,type).cost>available||!build_prerequisite(v,type))continue;
        const u32 bi=16+(type&15),limit=type==0x80||type==0x84?3:type==0x83?6:type==0x82?24:1;
        if(v.own_counts[bi]+v.pending_counts[bi]>=limit||v.pending_counts[bi]>=(type==0x82?2u:1u))continue;
        if(!full_plan_search) {
            if(type==0x80||type==0x83) {
                auto& plans=type==0x80?v.hq_build_plans:v.tower_build_plans;
                const auto& cached=type==0x80?s.cached_hq_plans:s.cached_tower_plans;
                for(u32 a=0;a<16;++a)plans[a]=revalidate(cached[a]);
                const u32 preferred=type==0x80?3:1;
                if(!plans[preferred].empty())v.macro_plans[12+i]=plans[preferred];
                else for(const auto& p:plans)if(!p.empty()){v.macro_plans[12+i]=p;break;}
            } else v.macro_plans[12+i]=revalidate(s.cached_build_plans[i]);
            continue;
        }
        if(type==0x80||type==0x83) {
            auto& plans=type==0x80?v.hq_build_plans:v.tower_build_plans;
            for(u32 a=0;a<16;++a) {
                // Placement anchors: towers only at our own places, the threat
                // point and the contested cluster; expansions only at the
                // expansion/contested cluster anchors. Searching every anchor
                // (enemy territory, unexplored starts) was the main view cost.
                if(type==0x83&&!(a<=3||a==10||a==13))continue;
                if(type==0x80&&!(a==3||a==13))continue;
                if(type==0x80) {
                    // HQ construction is an expansion, requiring an actual
                    // undeveloped, accessible berry cluster near its anchor.
                    if(!v.anchors[a].valid)continue;
                    CommanderPoint site;float best=640;
                    for(const auto& c:s.expansion.clusters)if(!c.developed&&c.site_explored&&c.site_x>=0&&c.known_amount>0) {
                        CommanderPoint p{c.site_x,c.site_y,true};const float d=distance(v.anchors[a],p);
                        if(d<best) {
                            const auto key=std::make_pair(p.x,p.y);
                            auto reachable=reachable_expansion.find(key);
                            if(reachable==reachable_expansion.end())reachable=reachable_expansion.emplace(key,!path(o,v.anchors[0],p).empty()).first;
                            if(reachable->second){best=d;site=p;}
                        }
                    }
                    if(site.valid)plans[a]=build_plan(s,placement_observation,v,type,site,occupancy);
                } else plans[a]=build_plan(s,placement_observation,v,type,v.anchors[a],occupancy,nullptr,8);
            }
            const u32 preferred=type==0x80?3:1;
            if(!plans[preferred].empty())v.macro_plans[12+i]=plans[preferred];
            else for(const auto& p:plans)if(!p.empty()){v.macro_plans[12+i]=p;break;}
        } else v.macro_plans[12+i]=build_plan(s,placement_observation,v,type,v.anchors[0],occupancy);
    }
    if(full_plan_search) {
        s.cached_hq_plans=v.hq_build_plans;s.cached_tower_plans=v.tower_build_plans;
        for(u32 i=0;i<build_types.size();++i)s.cached_build_plans[i]=v.macro_plans[12+i];
        s.plan_cache_frame=f;s.plan_cache_valid=true;s.plan_cache_workers=available_workers;s.plan_cache_available=available;s.plan_cache_terrain=terrain_key;
    }
    for(u32 i=0;i<research_orders.size();++i) {
        const u32 t=research_orders[i],level=o.research_order_levels[t];const bool multi=t==0x19||t==0x1a||t==0x1c||t==0x1d;
        if(level>=(multi?5u:1u))continue;
        if(services.curriculum_stage<3&&(t==0x1e||t==0x2a||t==0x2b))continue;
        if(multi&&level>=2&&!completed(v,t==0x19||t==0x1a?0x89:0x8a))continue;
        const u32 producer=t==0x14||t==0x2a||t==0x2b||t==0x38?0x80:
            t==0x19||t==0x1a||t==0x16?0x85:t==0x1c||t==0x1d||t==0x18?0x86:t==0x1b||t==0x2d?0x89:0x8a;
        const u32 cost=multi?200+200*level:info(services,AiProductionRequestKind::research,t).cost;
        if(cost>available)continue;
        bool in_progress=false;for(const auto& u:v.own)if(building(u)&&order_count(u,AiSemanticActionKind::research,t))in_progress=true;
        if(in_progress)continue;
        for(const auto& u:v.own)if(u.type_id==producer&&!u.under_construction&&u.deferred_command_count<10) {
            auto a=order(AiSemanticActionKind::research,u.id);a.production_id=t;if(valid(services,a)){v.macro_plans[22+i]={a};break;}
        }
    }
    if(s.merges.empty()) {
        for(u32 squad:{1u,0u,2u}) {
            // Merging stops both parents for 250 frames. Travelling or
            // defending squads must retain their fighting units.
            if(s.squads[squad].intent!=CommanderIntent::hold)continue;
            float best=std::numeric_limits<float>::max();AiSemanticAction pair;
            for(const auto& a:v.own)if(merge_material(a)&&s.units.at(a.id).squad==squad)for(const auto& b:v.own) {
                if(a.id>=b.id||a.type_id!=b.type_id||!merge_material(b)||s.units.at(b.id).squad!=squad)continue;
                const u32 gate=a.type_id==0x22?0x85:a.type_id==0x25?0x86:a.type_id==0x27?0x8a:0;
                if(!gate||!completed(v,gate))continue;
                const float d=distance(point(a),point(b));if(d<best){best=d;pair=order(AiSemanticActionKind::merge_units,a.id);pair.unit_ids.push_back(b.id);}
            }
            if(!pair.unit_ids.empty()){v.macro_plans[35]={pair};break;}
        }
        if(services.curriculum_stage>=3&&o.research_order_levels[0x18]) {
            for(u32 squad=0;squad<3;++squad) {
                AiSemanticAction a;a.kind=AiSemanticActionKind::merge_units;
                for(u32 t:{0x24u,0x27u,0x28u})for(const auto& u:v.own)if(u.type_id==t&&merge_material(u)&&s.units.at(u.id).squad==squad){a.unit_ids.push_back(u.id);break;}
                if(a.unit_ids.size()==3){v.macro_plans[36]={a};break;}
            }
        }
    }
    for(auto it=v.own.rbegin();it!=v.own.rend();++it)if(building(*it)&&(it->queued_production_type_id||it->deferred_command_count)) {
        auto a=order(AiSemanticActionKind::cancel_production,it->id);if(valid(services,a)){v.macro_plans[37]={a};break;}
    }
    fill_masks(v);
    bool damage_interrupt=false;
    for(u32 id:damaged_assets) {
        const auto it=s.damage_interrupt_frames.find(id);
        if(it==s.damage_interrupt_frames.end()||f-it->second>=88) {
            damage_interrupt=true;s.damage_interrupt_frames[id]=f;
        }
    }
    bool unlocked=false;for(u32 i=1;i<42;++i)if(v.mask[i]&&!s.last_mask[i])unlocked=true;
    bool squad_event=s.pending_reflex_event;s.pending_reflex_event=false;
    for(u32 q=0;q<3;++q)if(v.squads[q].center.valid) {
        const bool arrived=near(v.squads[q].center,v.anchors[s.squads[q].anchor],96);
        if(arrived&&!s.squads[q].arrived)squad_event=true;
        s.squads[q].arrived=arrived;
        if(s.squads[q].decision_weight>0&&v.squads[q].weight<s.squads[q].decision_weight*0.7f)squad_event=true;
    }
    if(!s.decision_count||(f>=1&&(f-1)%32==0))v.decision_due=true;
    else if(f-s.last_decision_frame>=8) {
        if(damage&&damage_interrupt){v.event=3;s.last_damage_interrupt=f;}
        else if(new_enemy)v.event=4;
        else if(squad_event)v.event=5;
        else if(spawn)v.event=2;
        else if(unlocked&&(s.last_economy_interrupt==0||f-s.last_economy_interrupt>=64)){v.event=1;s.last_economy_interrupt=f;}
        v.decision_due=v.event!=0;
    }
    s.last_mask=v.mask;s.last_view_frame=f;s.initialized=true;

    auto& z=v.input.vector;auto put=[&](u32 i,float x){z[i]=x;};
    put(0,norm(float(f),60000));put(1,float(f%128)/128);put(2+v.event,1);
    put(8,log_resource(o.primary_resources));put(9,norm(v.income_rate,100));put(10,norm(float(services.cumulative_gathered),60000));put(11,norm(float(v.workers),40));
    u32 idle_workers=0,harvesting=0,carrying=0,queues=0,constructions=0,researching=0,towers=0,engaged=0;float levels=0,investment=0,base_weight=0,base_health=0;
    for(const auto& u:v.own) {
        if(worker(u)){idle_workers+=u.command_state==1;harvesting+=u.command_state>=0x28&&u.command_state<=0x2d;carrying+=u.cargo_amount>0;}
        if(building(u)){queues+=u.deferred_command_count;constructions+=u.under_construction;researching+=ranker::researching(u);towers+=(u.type_id&15)==3;base_health+=hp(u);}
        if(combat(u)){investment+=s.units.at(u.id).investment;levels+=u.level;engaged+=(u.command_flags&0x10)!=0;if(near(point(u),v.anchors[0],640))base_weight+=weight(u);}
    }
    put(12,norm(float(idle_workers),10));put(13,norm(float(harvesting),40));put(14,v.workers?float(carrying)/v.workers:0);put(15,norm(float(bases.size()),3));
    put(16,norm(float(berry_amount[0]),32000));put(17,norm(float(berry_amount[1]),32000));
    u32 free_berries=0;for(auto t:base_tiles)if((o.tiles[t].terrain_flags&kMapCellReservedByUnit)==0)++free_berries;
    put(18,norm(float(free_berries),16));put(19,norm(float(queues),8));put(20,norm(float(constructions),3));put(21,norm(float(researching),3));
    put(22,norm(float(o.population_reserved),180));put(23,norm(float(o.population_used),180));put(24,std::clamp((float(o.population_used)-o.population_reserved)/16,-1.0f,1.0f));put(25,norm(float(v.queued_population),8));
    put(26,norm(float(v.army_count),40));put(27,norm(v.own_weight,20000));put(28,norm(investment,10000));put(29,norm(float(towers),4));put(30,v.army_count?norm(levels/v.army_count,5):0);put(31,v.army_count?float(engaged)/v.army_count:0);put(32,0);put(33,norm(base_weight,10000));
    put(34,norm(float(ec),20));put(35,norm(v.enemy_weight,20000));put(36,norm(mem_weight,20000));
    u32 vis_workers=0,known_buildings=0;float near_enemy_weight=0;
    for(const auto& g:v.enemies){vis_workers+=g.visible_now&&worker(g.seen);known_buildings+=building(g.seen);if(g.visible_now&&combat(g.seen))for(const auto& u:v.own)if(building(u)&&near(point(u),point(g.seen),640)){near_enemy_weight+=weight(g.seen);break;}}
    put(37,norm(float(vis_workers),10));put(38,norm(float(known_buildings),20));put(39,norm(float(enemy_bases),3));put(40,confirmed_enemy.valid);put(41,confirmed_enemy.valid?confirmed_enemy.x/4096.0f:0);put(42,confirmed_enemy.valid?confirmed_enemy.y/4096.0f:0);
    CommanderPoint memory_center=v.anchors[7];if(!memory_center.valid&&s.last_army_seen) {i64 x=0,y=0;u32 n=0;for(const auto& g:v.enemies)if(combat(g.seen)){x+=g.seen.x;y+=g.seen.y;++n;}if(n)memory_center={i32(x/n),i32(y/n),true};}
    put(43,memory_center.valid);put(44,memory_center.x/4096.0f);put(45,memory_center.y/4096.0f);put(46,norm(near_enemy_weight,10000));put(47,s.last_army_seen?norm(float(f-s.last_army_seen),2200):1);
    put(48,norm(float(s.max_enemy_count),8));put(49,std::clamp(std::log2((mem_weight+1)/(v.own_weight+1)),-3.0f,3.0f)/3);put(50,mem_weight>=1.2f*v.own_weight&&mem_weight>0);put(51,norm(float(s.waves_seen),5));put(52,s.last_wave_frame?norm(float(f-s.last_wave_frame),6600):1);
    put(53,s.threat.valid?norm(float(f-s.threat_frame),220):1);put(54,norm(float(s.damage_frames.size()),8));put(55,s.threat.x/4096.0f);put(56,s.threat.y/4096.0f);
    const auto building_count=std::count_if(v.own.begin(),v.own.end(),building);put(57,building_count?base_health/building_count:0);
    constexpr u32 tech[8]={0x14,0x16,0x1b,0x38,0x19,0x1a,0x1c,0x1d};for(u32 i=0;i<8;++i)put(58+i,float(o.research_order_levels[tech[i]])/(i<4?1:5));if(services.public_enemy_tribe<4)put(66+services.public_enemy_tribe,1);
    for(u32 i=0;i<32;++i){put(70+i,norm(float(v.own_counts[i]),i<16?10:4));put(102+i,norm(float(v.pending_counts[i]),4));}
    for(const auto& g:v.enemies)if(g.seen.type_id<0xa0)z[134+type_index(g.seen)]+=float(std::pow(0.5,double(f-g.last_seen_frame)/3000.0))/10;
    for(u32 q=0;q<3;++q) {
        const auto& squad=v.squads[q];const auto& ss=s.squads[q];const u32 off=166+q*21;float sumhp=0,minhp=1,spread=0,engage=0;std::array<u32,4> composition{};bool stalled=false;
        for(u32 id:squad.members){const auto& u=*find_unit(v.own,id);sumhp+=hp(u);minhp=std::min(minhp,hp(u));spread+=float(distance2(point(u),squad.center));engage+=(u.command_flags&0x10)!=0;stalled|=f-s.units.at(id).last_progress>=64;
            ++composition[u.type_id==0x2a||u.type_id==0x2d?3:u.render_class==3?2:u.attack_range_base>96?1:0];}
        const float n=float(squad.members.size());put(off,norm(n,20));put(off+1,norm(squad.weight,8000));put(off+2,norm(squad.investment,6000));put(off+3,squad.center.x/4096.0f);put(off+4,squad.center.y/4096.0f);put(off+5,n?norm(std::sqrt(spread/n),256):0);put(off+6,n?sumhp/n:0);put(off+7,n?minhp:0);
        for(u32 i=0;i<4;++i)put(off+8+i,norm(float(composition[i]),10));
        put(off+12,float(ss.intent)/8);put(off+13,float(ss.anchor)/16);put(off+14,float(ss.roe)/2);put(off+15,n?engage/n:0);put(off+16,norm(float(s.decision_count-ss.changed_decision),20));put(off+17,stalled);put(off+18,n?norm(distance(squad.center,v.anchors[0]),4096):0);put(off+19,n&&v.anchors[ss.anchor].valid?norm(distance(squad.center,v.anchors[ss.anchor]),4096):0);put(off+20,ss.automatic_retreat);
    }
    for(u32 a=0;a<16;++a) {
        const auto p=v.anchors[a];const u32 off=229+a*8;if(!p.valid)continue;put(off,1);put(off+1,p.x/4096.0f);put(off+2,p.y/4096.0f);float ownw=0,visw=0,memw=0;
        for(const auto& u:v.own)if(combat(u)&&near(point(u),p,320))ownw+=weight(u);
        for(const auto& g:v.enemies)if(combat(g.seen)&&near(point(g.seen),p,320)){memw+=weight(g.seen)*std::pow(0.97f,float(f-g.last_seen_frame)/64);if(g.visible_now)visw+=weight(g.seen);}
        put(off+3,norm(ownw,5000));put(off+4,norm(visw,5000));put(off+5,norm(memw,5000));put(off+6,norm(distance(main,p),4096));put(off+7,vision(o,p));
    }
    auto targets=v.enemies;
    std::sort(targets.begin(),targets.end(),[main](const auto& a,const auto& b){return std::make_tuple(a.visible_now?0:building(a.seen)?1:2,distance2(main,point(a.seen)),a.seen.id)<std::make_tuple(b.visible_now?0:building(b.seen)?1:2,distance2(main,point(b.seen)),b.seen.id);});
    for(u32 i=0;i<std::min<std::size_t>(12,targets.size());++i) {
        const auto& g=targets[i];const auto& u=g.seen;const u32 off=357+i*11,cls=building(u)?4:worker(u)?0:u.render_class==3?3:u.attack_range_base>96?2:1;put(off+cls,1);put(off+5,(u.x-main.x)/4096.0f);put(off+6,(u.y-main.y)/4096.0f);put(off+7,hp(u));put(off+8,g.visible_now);put(off+9,norm(float(f-g.last_seen_frame),2200));float isolation=0;for(const auto& other:v.enemies)if(combat(other.seen)&&near(point(other.seen),point(u),256))isolation+=weight(other.seen);put(off+10,norm(isolation,3000));
    }
    auto neutrals=v.neutrals;std::sort(neutrals.begin(),neutrals.end(),[main](const auto&a,const auto&b){return std::make_tuple(distance2(main,point(a.seen)),a.seen.id)<std::make_tuple(distance2(main,point(b.seen)),b.seen.id);});
    for(u32 i=0;i<std::min<std::size_t>(4,neutrals.size());++i) {
        const auto& u=neutrals[i].seen;const u32 off=489+i*5;put(off,(u.x-main.x)/4096.0f);put(off+1,(u.y-main.y)/4096.0f);put(off+2,norm(float(u.health),3000));put(off+3,norm(float(info(services,AiProductionRequestKind::unit,u.type_id).population),45));put(off+4,u.attack_power==0);
    }
    auto cell=[&](CommanderPoint p){return std::size_t(std::clamp(p.y/256,0,15))*16+std::clamp(p.x/256,0,15);};
    for(const auto& u:v.own){if(combat(u))v.input.map[cell(point(u))]+=weight(u)/2000;if(building(u))v.input.map[256+cell(point(u))]+=u.health/5000.0f;}
    for(const auto& g:v.enemies){const auto c=cell(point(g.seen));if(combat(g.seen)){if(g.visible_now)v.input.map[512+c]+=weight(g.seen)/2000;v.input.map[768+c]+=weight(g.seen)*std::pow(0.97f,float(f-g.last_seen_frame)/64)/2000;}if(building(g.seen))v.input.map[1024+c]+=g.seen.health/5000.0f;}
    std::array<u32,256> cells{};
    if(s.static_map_initialized)std::copy(s.static_map.begin(),s.static_map.end(),v.input.map.begin()+1792);
    for(std::size_t i=0;i<o.tiles.size();++i) {
        const auto c=cell({i32(i%o.map_width_tiles)*32,i32(i/o.map_width_tiles)*32,true});const auto&t=o.tiles[i];++cells[c];v.input.map[1280+c]+=t.resource_amount/32000.0f;v.input.map[1536+c]+=t.visible?1.0f:t.explored?0.5f:0;
        if(!s.static_map_initialized){v.input.map[1792+c]+=t.passable?1.0f:0;v.input.map[2048+c]+=t.placement_class>0?1.0f:0;}
    }
    for(u32 c=0;c<256;++c)if(cells[c])for(u32 channel=6;channel<(s.static_map_initialized?7u:9u);++channel)v.input.map[channel*256+c]/=cells[c];
    if(!s.static_map_initialized){std::copy(v.input.map.begin()+1792,v.input.map.end(),s.static_map.begin());s.static_map_initialized=true;}
    for(auto& value:v.input.map)value=std::round(std::clamp(value,0.0f,1.0f)*255)/255;
    for(u32 i=0;i<4;++i)if(o.start_candidate_mask&(1u<<i)) {
        CommanderPoint p{o.start_candidate_x[i],o.start_candidate_y[i],true};if(near(p,{o.start_x,o.start_y,true},160))put(509+i,1);if(confirmed_enemy.valid&&near(p,confirmed_enemy,320))put(513+i,1);
    }
    put(517,norm(float(unexplored),3));for(u32 i=0;i<8;++i)put(518+i,float(s.previous_action[i])/kCommanderHeadSizes[i]);put(526,s.decision_count?norm(float(f-s.last_decision_frame),220):0);put(527,norm(float(s.mask_violations+s.silent_rejections),10));
    const float tech_sum=float(std::accumulate(o.research_order_levels.begin(),o.research_order_levels.end(),0u));
    v.potential_components={0.25f*std::tanh((float(services.kills_investment)-services.losses_investment)/4000),
        0.10f*norm(float(services.cumulative_gathered),30000),0.05f*std::tanh(tech_sum/8),
        0.025f*std::clamp(float(bases.size())-1,0.0f,2.0f)};
    return v;
}

void CommanderLegalHeadMask(const CommanderView& v,const CommanderAction& p,std::size_t head,CommanderMask& mask) {
    if(head>=8)return;
    const auto off=kCommanderHeadOffsets[head],size=kCommanderHeadSizes[head];
    for(std::size_t i=0;i<size;++i)mask[off+i]=v.mask[off+i];
    if(head==1) {
        std::fill(mask.begin()+off,mask.begin()+off+size,0);
        if(p[0]==12||p[0]==14) {
            const auto& plans=p[0]==12?v.hq_build_plans:v.tower_build_plans;
            for(u32 a=0;a<16;++a)mask[off+a]=!plans[a].empty();
        } else mask[off]=1;
    }
    if(head>=3&&head<=5) {
        std::fill(mask.begin()+off,mask.begin()+off+size,0);
        if(p[2]==0||p[2]>3){mask[off]=1;return;}
        const auto& squad=v.squads[p[2]-1];
        if(head==3) {
            for(u32 i=0;i<8;++i)for(u32 a=0;a<16;++a)if(intent_anchor(v,i,a))mask[off+i]=1;
            if(near(squad.center,v.anchors[0],320))mask[off+7]=0;
            bool safe_hunt=false;
            for(const auto& g:v.neutrals)if(g.seen.attack_power==0&&near(point(g.seen),v.anchors[11],320)&&squad.weight>=1.5f*weight(g.seen))safe_hunt=true;
            if(!safe_hunt)mask[off+5]=0;
        } else if(head==4) {
            for(u32 a=0;a<16;++a)mask[off+a]=intent_anchor(v,p[3],a);
        } else for(u32 i=0;i<3;++i)mask[off+i]=1;
    }
    // Each conditional distribution always has a deterministic fallback.
    bool any=false;for(std::size_t i=0;i<size;++i)any|=mask[off+i]!=0;if(!any)mask[off]=1;
}

CommanderTeacherParams CommanderTeacherVariant(u32 variant) {
    CommanderTeacherParams p;if(!variant)return p;
    u32 h=variant*2654435761u+0x9e3779b9u;
    auto next=[&]{h^=h<<13;h^=h>>17;h^=h<<5;return h;};
    p.opening_velocis=2+next()%7;
    p.tower_frame=2000+next()%3001;
    p.attack_ratio=0.8f+float(next()%8)*0.1f;
    p.expansion_shift=i32(next()%6001)-3000;
    p.harass_period=(next()%3==0)?0:2000+next()%4001;
    p.target_priority=u8(next()%3);
    return p;
}

CommanderAction CommanderTeacherAction(const CommanderState& s,const CommanderView& v) {
    const CommanderTeacherParams P=CommanderTeacherVariant(v.services.teacher_variant);
    // Rule commander (teacher). Same masked head space as the learned policy.
    // Strategy uses only game structure and our own observations: economy
    // clock, chokepoints, matchups, towers, strength ratios from what we saw.
    CommanderAction a{};
    const auto& z=v.input.vector;const u32 f=v.frame;
    auto choose=[&](std::initializer_list<u32> options){for(u32 x:options)if(v.mask[x]){a[0]=u8(x);return true;}return false;};
    const auto count=[&](u32 t){return v.own_counts[(t>=0x60?16:0)+(t&15)]+v.pending_counts[(t>=0x60?16:0)+(t&15)];};
    const auto done=[&](u32 t){return v.own_counts[16+(t&15)];};
    const float res=std::expm1(z[8]*std::log1p(20000.0f));
    const u32 workers=v.workers+v.pending_counts[0],army=v.army_count;
    const float own_w=v.own_weight,mem_w=z[36]*20000,near_w=z[46]*10000,base_w=z[33]*10000;
    const float supply_margin=z[24]*16;
    const u32 tribe=v.services.public_enemy_tribe;
    bool enemy_air=false;for(u32 i=0;i<12;++i)if(z[357+i*11+3]>0)enemy_air=true;
    const bool ranged_need=tribe==1||tribe==3||enemy_air;
    const u32 velocis=count(0x22),dilo=count(0x24),bases=done(0x80);
    const u32 melee_core=velocis+2*count(0x23);
    const bool light_support=melee_core>=2&&count(0x21)<std::min(12u,2+melee_core/2)&&z[22]*180<120;
    const u32 worker_target=std::min(v.worker_cap,20u+8u*(bases>1?bases-1:0u));
    const bool emergency=near_w>=600&&near_w>base_w*0.8f;
    // Starving: no income for the last 220 frames late in the game. Saving
    // is pointless then; the army must decide the game with what exists.
    const bool starving=f>=15000&&z[9]<0.05f&&res<1000;
    const auto& guard=v.squads[1];const auto& main=v.squads[0];
    // Attack state with hysteresis: never flip attack/hold every decision
    // (each flip re-marches the whole squad); only a real home threat
    // interrupts immediately. MAIN attacks only from a measured strength
    // advantage or right after a repelled wave.
    bool attacking=s.squads[0].intent==CommanderIntent::attack_move||s.squads[0].intent==CommanderIntent::siege;
    const u32 since_change=s.decision_count-s.squads[0].changed_decision;
    const bool base_danger=near_w>0&&base_w<near_w;
    // MAIN must carry most of the army before leaving; a lone scout passing
    // our buildings is not a repelled wave, so the counter-attack clause
    // needs a real force at hand as well.
    const bool main_ready=main.weight>=0.55f*own_w;
    // An opponent on a closed plateau (single ramp, towers above) is only
    // assaulted with siege range in hand and a bigger force. The enemy start
    // slot is public map information (one-hot at 513..516; slots 0/1 are the
    // plateaus of this map).
    const bool enemy_plateau=z[513]>0||z[514]>0;
    const bool siege_ready=!enemy_plateau||(count(0x2a)>=2&&army>=20)||army>=30;
    // Population cap: nothing more can be built, so the army must be used.
    const bool pop_capped=z[22]*180>=168;
    const bool desperate=(starving||pop_capped)&&army>=8&&main_ready;
    // Fresh intelligence: the enemy army was seen within ~1100 frames, so the
    // remembered strength is meaningful. Without it a small scouting party is
    // sent first (RAID rules below) instead of marching blind into a base.
    const bool fresh=z[47]<0.5f;
    const bool can_attack=desperate||(main_ready&&siege_ready&&fresh&&((army>=16&&own_w>=4000&&own_w>=P.attack_ratio*mem_w+1500)||
        (s.waves_seen&&f-s.last_wave_frame>300&&army>=14&&own_w>=3500&&own_w>=P.attack_ratio*mem_w&&near_w==0)));
    const auto& raid=v.squads[2];
    const bool must_stop=army<6||(own_w<0.6f*mem_w&&!desperate)||base_danger;
    if(attacking&&must_stop&&(base_danger||since_change>=10))attacking=false;
    else if(!attacking&&can_attack&&!base_danger&&since_change>=20)attacking=true;
    const bool want_scout=!attacking&&!fresh&&army>=12&&f>=6000&&near_w==0&&!starving&&
        (v.anchors[4].valid||v.anchors[12].valid)&&
        (!s.last_transfer_frame[3]||f-s.last_transfer_frame[3]>=1100);
    // Variant harassment: a small fast party raids known enemy workers
    // periodically while the main army stays home.
    const bool raid_harassing=s.squads[2].intent==CommanderIntent::harass;
    const bool want_harass=P.harass_period&&!attacking&&army>=16&&near_w==0&&!starving&&v.anchors[8].valid&&
        (!s.last_transfer_frame[3]||f-s.last_transfer_frame[3]>=P.harass_period);
    const bool recall_scout=!raid.members.empty()&&(attacking||near_w>0||army<8||
        f-s.last_transfer_frame[2]>=1100||(!raid_harassing&&fresh&&!want_harass));
    // Economy clock: the home berry window (8 tiles, 32k) runs dry near
    // 15k frames with 20 workers; the expansion must stand before that.
    const float home_berries=z[16];
    const bool expansion_pending=v.pending_counts[16]!=0;
    // Further bases follow before both windows run dry (each window feeds
    // 20 workers for roughly 15k frames).
    const u32 expansion_frame=u32(std::max(3000,8000+P.expansion_shift));
    const bool expansion_due=bases<4&&!expansion_pending&&(f>=expansion_frame||home_berries<0.55f)&&
        (bases<2||(home_berries<0.35f&&z[17]<0.35f));
    const bool saving=expansion_due&&res<1000&&!emergency&&!starving;
    // --- H1 macro: strict priority list; the first affordable/legal option wins.
    // Keep the main melee line, with bounded low-cost support when the
    // preferred unit is unaffordable and population is still plentiful.
    if(emergency)choose({3,4});
    if(!a[0]&&emergency&&light_support)choose({2});
    if(!a[0]&&supply_margin<3&&v.pending_counts[18]==0)choose({13});
    if(!a[0]&&workers<8)choose({1});
    if(!a[0]&&expansion_due&&res>=1000&&near_w==0)choose({12});
    if(!a[0]&&!count(0x84))choose({15});
    if(!a[0]&&workers<14)choose({1});
    if(!a[0]&&velocis<P.opening_velocis)choose({3});
    if(!a[0]&&count(0x83)<1&&(f>=P.tower_frame||near_w>0))choose({14});
    // Squad balance: GUARD keeps about a third of the army at home; a large
    // idle GUARD folds into MAIN once MAIN is back home. Never peel units
    // from an attacking MAIN just after reinforcements joined it. At home,
    // allow the previous transfer to settle before reversing its direction.
    const bool rebalance_ready=f-std::max(s.last_transfer_frame[0],s.last_transfer_frame[1])>=440;
    if(!a[0]&&!attacking&&!desperate&&rebalance_ready&&guard.members.empty()&&main.members.size()>=4)choose({38});
    if(!a[0]&&!attacking&&rebalance_ready&&near_w==0&&guard.weight>1800&&guard.weight>1.4f*main.weight&&!main.members.empty()&&near(main.center,guard.center,640))choose({39});
    if(!a[0]&&(starving||pop_capped)&&!attacking&&!guard.members.empty()&&!main.members.empty()&&guard.weight>main.weight)choose({39});
    // Scouting party: a few fast units leave MAIN, one of them scouts the
    // enemy base. Give it travel time in simulation frames, independent of
    // interrupts. A newly filled RAID receives SCOUT on the next decision;
    // its previous HOLD intent is not a reason to recall it immediately.
    if(!a[0]&&(want_scout||want_harass)&&raid.members.empty()&&main.members.size()>=8)choose({40});
    if(!a[0]&&recall_scout)choose({41});
    if(!a[0]&&workers<worker_target)choose({1});
    if(!a[0]&&!saving&&count(0x84)<2&&workers>=12)choose({15});
    if(!a[0]&&!saving&&workers>=12)choose({22});
    if(!a[0]&&count(0x83)<2&&f>=3000)choose({14});
    if(!a[0]&&bases>=2&&count(0x83)<3)choose({14});
    if(!a[0]&&!saving&&velocis<6)choose({3});
    // Observed opponent composition (relative-indexed memory counts): a
    // large basic-unit swarm calls for splash (egg throwers) and more towers.
    const float basic_enemy=(z[135]+z[136])*10;
    const bool swarm=basic_enemy>=15;
    if(!a[0]&&!saving&&swarm&&count(0x83)<4&&res>=400)choose({14});
    if(!a[0]&&!saving&&(swarm||(enemy_plateau&&f>=6000))&&!count(0x87)&&done(0x84)>=2&&res>=500)choose({18});
    if(!a[0]&&!saving&&(swarm||enemy_plateau)&&done(0x87)&&count(0x2a)<(swarm?4u:3u)&&res>=650)choose({9});
    // Reinforcements: units gathered at home while MAIN is out join it in a
    // group once enough have accumulated and home is quiet.
    if(!a[0]&&attacking&&guard.weight>3000&&near_w==0&&!guard.members.empty())choose({39});
    // Ranged support: a dilophos line behind the melee core (always at
    // least a third against ranged/air-heavy opponents, a fifth otherwise).
    if(!a[0]&&!saving&&dilo<std::max(1u,velocis/(ranged_need?3u:5u)))choose({4});
    if(!a[0]&&!saving&&!count(0x85)&&f>=2800&&workers>=12)choose({16});
    if(!a[0]&&!saving&&done(0x85)&&army>=4&&res>=250){if(z[62]<0.2f)choose({25});if(!a[0]&&z[63]<0.2f)choose({26});}
    if(!a[0]&&!saving&&count(0x84)<3&&f>=7000&&res>=600)choose({15});
    if(!a[0]&&!saving&&army>=10&&res>=600&&z[59]<0.5f)choose({23});
    if(!a[0]&&!saving&&done(0x85)&&res>=450){if(z[62]<0.4f)choose({25});if(!a[0]&&z[63]<0.4f)choose({26});}
    if(!a[0]&&!saving&&f>=10000&&!count(0x87)&&res>=800)choose({18});
    if(!a[0]&&!saving&&f>=10000&&count(0x2a)<2&&res>=700)choose({9});
    // Rich late game (often at the population cap): convert the bank into
    // technology — upgrade nest, land nisdos, attack/defense to level 5,
    // melee reinforcement — instead of letting it idle.
    if(!a[0]&&res>=1200&&done(0x85)&&!count(0x88))choose({19});
    if(!a[0]&&res>=1200&&done(0x88)&&done(0x85)&&!count(0x89))choose({20});
    if(!a[0]&&res>=800&&done(0x89)){if(z[62]<1.0f)choose({25});if(!a[0]&&z[63]<1.0f)choose({26});if(!a[0]&&z[60]<0.5f)choose({27});}
    // Merging immobilizes both units for 250 ticks: only with no enemy in sight.
    const bool quiet=v.enemy_weight==0&&near_w==0;
    const auto& twins=v.macro_plans[35];
    const bool safe_merge=quiet&&!twins.empty()&&!twins[0].unit_ids.empty()&&
        (!attacking||s.units.at(twins[0].unit_ids[0]).squad!=0);
    if(!a[0]&&safe_merge&&velocis>=6&&done(0x85)&&supply_margin<6)choose({35});
    if(!a[0]&&!saving)choose({3,4});
    if(!a[0]&&!saving&&light_support)choose({2});
    if(!a[0]&&safe_merge&&velocis>=8&&done(0x85))choose({35});
    if(a[0]==12)a[1]=3;
    else if(a[0]==14)a[1]=(count(0x83)==0&&v.closed_plateau&&v.anchors[2].valid)?2:(bases>=2&&count(0x83)>=2&&v.anchors[3].valid?3:1);
    // --- Squads: GUARD holds home (aggressive ROE, towers help); MAIN
    // attacks only from a measured strength advantage or after a repelled
    // wave, and comes home when the advantage is gone.
    // Respond to a raid on our assets only if it is not a superior army:
    // leaving the towers to meet a bigger force in the open loses everything.
    const bool threat=v.anchors[10].valid&&near_w>0&&near_w<=0.9f*own_w;
    // Home post: on a closed plateau the army holds the single ramp together
    // with its tower; elsewhere the forward defense point. GUARD escorts the
    // expansion only while the new HQ is under construction (no tower yet).
    const u8 home=(v.closed_plateau&&done(0x83)>0&&v.anchors[2].valid)?2:1;
    const bool cover_expansion=v.anchors[3].valid&&expansion_pending;
    u8 gi=u8(CommanderIntent::hold),ga=cover_expansion?3:home,gr=0;
    // Early idle time: hunt harmless wild dinosaurs near home for free
    // experience levels (the HUNT mask only opens when the hunt is safe).
    // Only inside the home area: chasing fleeing animals drew the guard out
    // of the plateau and it was caught alone by the first raid.
    if(!threat&&!cover_expansion&&!v.closed_plateau&&f<6000&&guard.weight>=900&&v.mask[62+u8(CommanderIntent::hunt)]&&v.anchors[11].valid&&near(v.anchors[11],v.anchors[0],320)){gi=u8(CommanderIntent::hunt);ga=11;}
    if(threat){gi=u8(CommanderIntent::defend);ga=10;}
    u8 mi=u8(CommanderIntent::hold),ma=home,mr=0;
    if(attacking) {
        mr=1;mi=u8(CommanderIntent::attack_move);
        // Finish the opponent: known building → enemy expansion → unexplored
        // start → contested cluster → enemy HQ estimate.
        // Variant target priority: enemy expansion or workers first when
        // actually known. Anchor 9 without a building ghost near it is only
        // the estimated natural-expansion site: marching there parks the army
        // on empty ground (variants 4/9/11 lost almost every game that way).
        bool enemy_expansion_known=false;
        if(v.anchors[9].valid)for(const auto& g:v.enemies)if(building(g.seen)&&near(point(g.seen),v.anchors[9],640)){enemy_expansion_known=true;break;}
        if(P.target_priority==1&&enemy_expansion_known)ma=9;
        else if(P.target_priority==2&&v.anchors[8].valid)ma=8;
        else if(v.anchors[6].valid){ma=6;if(near(main.center,v.anchors[6],640))mi=u8(CommanderIntent::siege);}
        else if(v.anchors[9].valid)ma=9;
        else if(v.anchors[12].valid)ma=12;
        else if(v.anchors[13].valid)ma=13;
        else ma=4;
    } else if(threat){mi=u8(CommanderIntent::defend);ma=10;}
    auto differs=[&](u32 q,u8 i,u8 an,u8 r){return u8(s.squads[q].intent)!=i||s.squads[q].anchor!=an||u8(s.squads[q].roe)!=r;};
    const u8 scout_anchor=v.anchors[4].valid?4:12;
    if(!guard.members.empty()&&differs(1,gi,ga,gr)){a[2]=2;a[3]=gi;a[4]=ga;a[5]=gr;}
    else if(!main.members.empty()&&differs(0,mi,ma,mr)){a[2]=1;a[3]=mi;a[4]=ma;a[5]=mr;}
    else if(!raid.members.empty()&&!recall_scout&&want_harass&&differs(2,u8(CommanderIntent::harass),8,1)){a[2]=3;a[3]=u8(CommanderIntent::harass);a[4]=8;a[5]=1;}
    else if(!raid.members.empty()&&!recall_scout&&!raid_harassing&&v.anchors[scout_anchor].valid&&differs(2,u8(CommanderIntent::scout),scout_anchor,2)){a[2]=3;a[3]=u8(CommanderIntent::scout);a[4]=scout_anchor;a[5]=2;}
    // Workers stay AUTO: the executor already moves an individually
    // threatened worker away, while a blanket FLEE stalls the whole economy
    // for as long as any enemy lingers near any building.
    a[6]=0;
    // Grow both squads in proportion during the opening. A fixed 1800 GUARD
    // target left only one fighter in MAIN until repeated full transfers.
    // While MAIN attacks, new units gather in GUARD before joining as a group.
    const float guard_target=std::clamp(own_w/3.0f,660.0f,1800.0f);
    a[7]=(attacking||guard.weight<guard_target)?1:0;
    CommanderMask mask=v.mask;
    for(std::size_t h=0;h<8;++h) {
        CommanderLegalHeadMask(v,a,h,mask);const auto off=kCommanderHeadOffsets[h];
        if(a[h]>=kCommanderHeadSizes[h]||!mask[off+a[h]]) {
            a[h]=0;for(u32 i=0;i<kCommanderHeadSizes[h];++i)if(mask[off+i]){a[h]=u8(i);break;}
        }
    }
    return a;
}

std::vector<AiSemanticAction> CommanderExecute(CommanderState& s,const AiObservation& o,
    const CommanderView& v,const CommanderAction* decision) {
    std::vector<AiSemanticAction> output;const u32 f=v.frame;
    if(f<1||(f-1)%8!=0)return output;
    const u32 window=(f-1)/32;if(window!=s.packet_window){s.packet_window=window;s.packets_in_window=0;}
    u32 frame_packets=0,spent=0,directed_squad=3;std::set<u32> commanded;
    auto publish=[&](AiSemanticAction a,bool remember=true) {
        const auto packets=u32(a.unit_ids.size());if(packets==0||frame_packets+packets>std::min(64u,v.services.packet_budget)||s.packets_in_window+packets>256)return false;
        if(a.target_unit_id&&!find_unit(v.visible_enemies,a.target_unit_id)&&!find_unit(v.visible_neutrals,a.target_unit_id))return false;
        if(a.kind==AiSemanticActionKind::move&&a.target_unit_id)return false;
        if(!valid(v.services,a))return false;
        for(u32 id:a.unit_ids) {
            commanded.insert(id);auto it=s.units.find(id);
            if(remember&&it!=s.units.end()){it->second.last_order=a;it->second.last_order_frame=f;}
        }
        frame_packets+=packets;s.packets_in_window+=packets;output.push_back(std::move(a));return true;
    };
    auto issue_point=[&](const AiObservedUnit& u,AiSemanticActionKind kind,CommanderPoint p,bool force=false) {
        p=u.render_class==3?clamp_point(o,p):ground_point(o,p);auto a=order(kind,u.id,p);auto& state=s.units.at(u.id);
        if(!force&&same_order(a,state.last_order)&&u.command_state!=1&&f-state.last_progress<64)return false;
        if(!force&&f-state.last_order_frame<8&&state.last_order.kind!=AiSemanticActionKind::no_op)return false;
        return publish(a);
    };
    auto apply_build=[&](const AiSemanticAction& a) {
        if(a.unit_ids.empty()||commanded.count(a.unit_ids[0]))return false;
        const auto footprint=AiBuildingFootprintOf(a.production_id);
        const i32 x=a.target_x/32,y=a.target_y/32;
        for(const auto& existing:s.builds) {
            const auto occupied=AiBuildingFootprintOf(existing.order.production_id);
            const i32 bx=existing.order.target_x/32,by=existing.order.target_y/32;
            if(x<=bx+i32(occupied.width)&&x+i32(footprint.width)>=bx&&
                y<=by+i32(occupied.height)&&y+i32(footprint.height)>=by)return false;
        }
        const auto cost=info(v.services,AiProductionRequestKind::building,a.production_id).cost;
        if(u64(spent)+v.reserved_resources+cost>o.primary_resources||!publish(a))return false;
        s.builds.push_back({a,cost,f,1,false,s.units.at(a.unit_ids[0]).generation});spent+=cost;
        diagnose(v.services,"build-issued",f,a,find_unit(v.own,a.unit_ids[0]),o.primary_resources);
        float best=std::numeric_limits<float>::max();CommanderPoint harvest;
        for(std::size_t t=0;t<o.tiles.size();++t)if(o.tiles[t].resource_amount&&o.tiles[t].explored) {
            CommanderPoint p{i32(t%o.map_width_tiles)*32,i32(t/o.map_width_tiles)*32,true};
            const float d=distance(p,{a.target_x,a.target_y,true});if(d<best){best=d;harvest=p;}
        }
        // Do not enqueue behind a merely pending BUILD. A harvest/dropoff
        // transition can pop its queue before the pending build enters and
        // overwrite that build with the queued harvest. Wait for creation.
        s.units.at(a.unit_ids[0]).post_build_harvest=harvest;
        return true;
    };
    for(const auto& u:v.own)if(u.under_construction&&v.services.construction_progress&&v.services.construction_progress(u.id)<0.3f&&
        s.threat.valid&&f-s.threat_frame<=8&&near(point(u),s.threat,32)) {
        publish(order(AiSemanticActionKind::cancel_construction,u.id));
    }
    if(decision) {
        CommanderAction action=*decision;CommanderMask mask=v.mask;
        for(std::size_t h=0;h<8;++h) {
            CommanderLegalHeadMask(v,action,h,mask);const auto off=kCommanderHeadOffsets[h];
            if(action[h]>=kCommanderHeadSizes[h]||!mask[off+action[h]]) {
                ++s.mask_violations;action[h]=0;for(u32 i=0;i<kCommanderHeadSizes[h];++i)if(mask[off+i]){action[h]=u8(i);break;}
            }
        }
        ++s.decision_count;s.last_decision_frame=f;s.previous_action=action;s.worker_policy=action[6];s.rally_squad=action[7];
        if(action[2]>=1&&action[2]<=3) {
            directed_squad=action[2]-1;
            auto& q=s.squads[action[2]-1];const auto intent=CommanderIntent(action[3]);const auto roe=CommanderRoe(action[5]);
            if(q.intent!=intent||q.anchor!=action[4]||q.roe!=roe){q.intent=intent;q.anchor=action[4];q.roe=roe;++q.serial;q.changed_decision=s.decision_count;}
            q.automatic_retreat=false;q.decision_weight=v.squads[action[2]-1].weight;
        }
        const u32 macro=action[0];
        if(macro>=38) {
            const u32 source=macro==39?1:macro==41?2:0,dest=macro==38?1:macro==40?2:0;
            auto ids=v.squads[source].members;
            if(macro==38)std::sort(ids.begin(),ids.end(),[&](u32 a,u32 b){return std::make_tuple(-i64(s.units.at(a).investment),a)<std::make_tuple(-i64(s.units.at(b).investment),b);});
            if(macro==40)std::sort(ids.begin(),ids.end(),[&](u32 a,u32 b){return std::make_tuple(-speed(*find_unit(v.own,a)),a)<std::make_tuple(-speed(*find_unit(v.own,b)),b);});
            const auto n=macro==38?(ids.size()+2)/3:macro==40?std::min<std::size_t>(4,ids.size()):ids.size();
            for(std::size_t i=0;i<n;++i){s.units.at(ids[i]).squad=u8(dest);s.units.at(ids[i]).applied_intent_serial=0;}
            if(n)s.last_transfer_frame[macro-38]=f;
        } else if(macro==35||macro==36) {
            if(!v.macro_plans[macro].empty()) {
                CommanderMergeReservation m;m.units=v.macro_plans[macro][0].unit_ids;m.started_frame=f;
                for(u32 id:m.units){const auto& u=*find_unit(v.own,id);m.center.x+=u.x;m.center.y+=u.y;m.investment+=s.units.at(id).investment;m.squad=s.units.at(id).squad;m.generations.push_back(s.units.at(id).generation);}
                if(!m.units.empty()){m.center.x/=i32(m.units.size());m.center.y/=i32(m.units.size());m.center.valid=true;s.merges.push_back(m);}
            }
        } else if(macro>0) {
            const auto& plans=macro==12?v.hq_build_plans[action[1]]:macro==14?v.tower_build_plans[action[1]]:v.macro_plans[macro];
            for(const auto& a:plans) {
                if(a.kind==AiSemanticActionKind::build)apply_build(a);
                else if(publish(a)) {
                    const auto kind=a.kind==AiSemanticActionKind::research?AiProductionRequestKind::research:AiProductionRequestKind::unit;
                    if(a.kind==AiSemanticActionKind::produce_unit||a.kind==AiSemanticActionKind::research) {
                        spent+=info(v.services,kind,a.production_id).cost;
                        const auto* source=find_unit(v.own,a.unit_ids[0]);
                        s.receipts.push_back({a,f,source?order_count(*source,a.kind,a.production_id):0,
                            a.kind==AiSemanticActionKind::research&&a.production_id<64?o.research_order_levels[a.production_id]:0u,
                            s.units.at(a.unit_ids[0]).generation});
                    }
                }
            }
        }
    }
    // Reserve supply before spending workers' outstanding construction costs.
    if(o.population_reserved+v.queued_population+4>=o.population_used&&v.pending_counts[18]==0) {
        const bool reserved=std::any_of(s.builds.begin(),s.builds.end(),[](const auto& b){return b.order.production_id==0x82;});
        if(!reserved&&!v.macro_plans[13].empty())apply_build(v.macro_plans[13][0]);
    }
    // Retry only commands which never entered the worker's build state.
    // Walking builders keep their cost reservation until structure creation.
    for(auto& r:s.builds)if(!r.acknowledged&&f-r.issued_frame>=16&&r.attempts<3) {
        const auto* u=r.order.unit_ids.empty()?nullptr:find_unit(v.own,r.order.unit_ids[0]);if(!u||commanded.count(u->id))continue;
        diagnose(v.services,"build-retry",f,r.order,u,o.primary_resources);
        AiObservation placement=o;placement.units=v.own;
        placement.units.insert(placement.units.end(),v.visible_enemies.begin(),v.visible_enemies.end());
        placement.units.insert(placement.units.end(),v.visible_neutrals.begin(),v.visible_neutrals.end());
        const CommanderPoint shifted{r.order.target_x+(r.attempts%2?64:-64),r.order.target_y+32,true};
        const auto candidates=build_plan(s,placement,v,r.order.production_id,shifted,v.build_occupancy,&r);
        if(!candidates.empty()&&!commanded.count(candidates[0].unit_ids[0])&&publish(candidates[0])) {
            const auto harvest=s.units.at(u->id).post_build_harvest;s.units.at(u->id).post_build_harvest={};
            s.units.at(candidates[0].unit_ids[0]).post_build_harvest=harvest;
            r.order=candidates[0];r.source_generation=s.units.at(r.order.unit_ids[0]).generation;r.issued_frame=f;++r.attempts;
        }
        else {r.issued_frame=f;++r.attempts;}
    }
    for(const auto& u:v.own)if(worker(u)&&!construction_worker(s,u.id)&&!commanded.count(u.id)) {
        auto& state=s.units.at(u.id);if(!state.post_build_harvest.valid)continue;
        const u32 active=u.command_state&0x00ffffffu;
        const bool constructing=active==0x24||active==0x5b||active==0x7e;
        if(!constructing&&active!=1)continue;
        const auto tile=tile_index(o,state.post_build_harvest);
        if(tile>=o.tiles.size()||!o.tiles[tile].resource_amount){state.post_build_harvest={};continue;}
        auto next=order(AiSemanticActionKind::harvest,u.id,state.post_build_harvest);next.queued=constructing;
        if(publish(next,false)){state.harvest_tile=i32(tile);state.post_build_harvest={};}
    }
    std::set<u32> merging;
    for(auto it=s.merges.begin();it!=s.merges.end();) {
        if(!it->issued&&s.squads[it->squad].intent!=CommanderIntent::hold){it=s.merges.erase(it);continue;}
        if(f-it->started_frame>700){it=s.merges.erase(it);continue;}
        bool all=true,ready=true;for(std::size_t index=0;index<it->units.size();++index) {
            const u32 id=it->units[index];
            if(index>=it->generations.size()||!matching_identity(s,id,it->generations[index])){all=false;continue;}
            merging.insert(id);const auto* u=find_unit(v.own,id);if(!u){all=false;continue;}
            if(!near(point(*u),it->center,5))ready=false;
        }
        if(!all){if(!it->issued)it=s.merges.erase(it);else ++it;continue;}
        if(!it->issued) {
            if(ready) {AiSemanticAction a;a.kind=AiSemanticActionKind::merge_units;a.unit_ids=it->units;if(publish(a)){it->issued=true;it->started_frame=f;}}
            else for(u32 id:it->units){const auto& u=*find_unit(v.own,id);if(!busy(u))issue_point(u,AiSemanticActionKind::move,it->center);}
        }
        ++it;
    }
    // Automatic defense and ROE use only the currently visible force. The
    // reflex answers raids on our buildings and the workers around them; a
    // worker hit far away (scout, distant berries) must not bait the army.
    bool threat_at_assets=false;
    if(s.threat.valid)for(const auto& u:v.own)if(building(u)&&near(point(u),s.threat,640)){threat_at_assets=true;break;}
    if(s.threat.valid&&f-s.threat_frame<=8&&threat_at_assets) {
        u32 q=1;if(v.squads[1].members.empty())q=v.squads[0].members.empty()?2:0;
        // The commander has just observed this threat. Its explicit order
        // for this squad wins over the same-tick reflex (e.g. holding towers
        // instead of escorting a doomed expansion). NONE leaves the reflex on.
        if(q!=directed_squad&&!v.squads[q].members.empty()) {
            auto& sq=s.squads[q];
            if(sq.intent!=CommanderIntent::defend||sq.anchor!=10) {
                sq.intent=CommanderIntent::defend;sq.anchor=10;++sq.serial;s.pending_reflex_event=true;
            }
        }
    }
    for(u32 q=0;q<3;++q) {
        const auto& sq=v.squads[q];auto& rule=s.squads[q];if(!sq.center.valid)continue;
        float friendly=0,enemy=0;for(const auto& u:v.own)if(combat(u)&&near(point(u),sq.center,320))friendly+=weight(u);
        for(const auto& u:v.visible_enemies)if(combat(u)&&near(point(u),sq.center,320))enemy+=weight(u);
        const float threshold=rule.roe==CommanderRoe::aggressive?0.4f:rule.roe==CommanderRoe::normal?0.7f:1.0f;
        bool flee=enemy>0&&friendly/enemy<threshold;
        if(rule.intent==CommanderIntent::harass) {
            float close=0;for(const auto& u:v.visible_enemies)if(combat(u)&&near(point(u),sq.center,192))close+=weight(u);
            flee|=close>0&&close*1.2f>=sq.weight;
        }
        // Never abandon a home fight: near our HQ, defense point, choke or
        // expansion the squad fights with its towers instead of walking
        // (unarmed, mid-swing) through the attackers toward the HQ.
        bool at_home=false;
        for(u32 a:{0u,1u,2u,3u})if(v.anchors[a].valid&&near(sq.center,v.anchors[a],640))at_home=true;
        if(flee&&!at_home&&rule.intent!=CommanderIntent::retreat&&distance(sq.center,v.anchors[0])>192) {
            rule.intent=CommanderIntent::retreat;rule.anchor=0;rule.automatic_retreat=true;++rule.serial;s.pending_reflex_event=true;
        }
        if(rule.intent==CommanderIntent::defend&&!v.anchors[10].valid){rule.intent=CommanderIntent::hold;rule.anchor=1;++rule.serial;}
    }
    if(v.services.autoscout&&f>=300&&s.scout_id==0&&v.anchors[12].valid&&!v.input.vector[40]) {
        for(const auto& u:v.own)if(worker(u)&&!construction_worker(s,u.id)&&!commanded.count(u.id)&&!busy(u)){s.scout_id=u.id;break;}
    }
    if(s.scout_id) {
        const auto* u=find_unit(v.own,s.scout_id);
        if(!u)s.scout_id=0;
        else if(v.input.vector[40]||!v.anchors[12].valid||hp(*u)<0.5f) {
            if(near(point(*u),v.anchors[0],256))s.scout_id=0;else if(!commanded.count(u->id))issue_point(*u,AiSemanticActionKind::move,v.anchors[0]);
        } else if(!commanded.count(u->id))issue_point(*u,AiSemanticActionKind::move,v.anchors[12]);
    }
    // Current assignments, including healthy engine harvest loops, count
    // toward per-window 2.5 workers/tile. Idle workers get one order only.
    std::map<std::size_t,u32> assigned;std::vector<std::pair<CommanderPoint,std::vector<std::size_t>>> windows;
    std::set<std::size_t> claimed;std::vector<std::size_t> far_tiles;bool far_tiles_ready=false;
    for(const auto& base:v.own)if(base.type_id==0x80&&!base.under_construction) {
        windows.push_back({point(base),{}});auto& tiles=windows.back().second;
        for(i32 y=base.y/32-15;y<=base.y/32+15;++y)for(i32 x=base.x/32-15;x<=base.x/32+15;++x) {
            const auto t=tile_index(o,{x*32,y*32,true});if(t<o.tiles.size()&&o.tiles[t].resource_amount&&o.tiles[t].explored&&claimed.insert(t).second)tiles.push_back(t);
        }
    }
    for(const auto& u:v.own)if(worker(u)) {
        auto& state=s.units.at(u.id);if(u.command_state>=0x28&&u.command_state<=0x2d) {
            auto tile=state.harvest_tile>=0?std::size_t(state.harvest_tile):tile_index(o,{u.destination_x,u.destination_y,true});
            if(tile<o.tiles.size()&&o.tiles[tile].resource_amount){++assigned[tile];state.harvest_tile=i32(tile);}
        }
    }
    for(const auto& u:v.own)if(worker(u)&&u.id!=s.scout_id&&!construction_worker(s,u.id)&&!commanded.count(u.id)&&!busy(u)) {
        bool threatened=false;for(const auto& enemy:v.visible_enemies)if(combat(enemy)&&near(point(u),point(enemy),192)){threatened=true;break;}
        if(s.worker_policy==1||(s.worker_policy==0&&threatened)) {
            auto p=v.anchors[0];if(s.threat.valid){p.x+=(p.x-s.threat.x)>=0?128:-128;p.y+=(p.y-s.threat.y)>=0?128:-128;}
            issue_point(u,AiSemanticActionKind::move,p);continue;
        }
        if(s.worker_policy==2&&v.anchors[10].valid){issue_point(u,AiSemanticActionKind::attack_move,v.anchors[10]);continue;}
        auto& state=s.units.at(u.id);
        if(u.command_state!=1)continue; // Engine owns normal gathering/return loops.
        if(u.cargo_amount) {auto a=order(AiSemanticActionKind::return_cargo,u.id);if(publish(a))continue;}
        float best=std::numeric_limits<float>::max();std::size_t target=o.tiles.size();
        for(const auto& w:windows) {
            u32 workers=0;for(auto t:w.second)workers+=assigned[t];if(workers>=w.second.size()*5/2)continue;
            for(auto t:w.second) {
                if(assigned[t]>=3)continue;
                CommanderPoint p{i32(t%o.map_width_tiles)*32+16,i32(t/o.map_width_tiles)*32+16,true};
                const float score=distance(point(u),w.first)+distance(point(u),p)+assigned[t]*256+(o.tiles[t].resource_amount<500?1024:0);
                if(score<best){best=score;target=t;}
            }
        }
        // Fallbacks against idle workers late in the game: first exceed the
        // per-window 2.5/tile target (up to 3/tile) rather than idle, then
        // walk to the nearest known berries within 48 tiles of any HQ once
        // the home windows are dry. A long trip still beats zero income and
        // keeps the expansion fund growing.
        if(target>=o.tiles.size())for(const auto& w:windows)for(auto t:w.second) {
            if(assigned[t]>=3)continue;
            CommanderPoint p{i32(t%o.map_width_tiles)*32+16,i32(t/o.map_width_tiles)*32+16,true};
            const float score=distance(point(u),w.first)+distance(point(u),p)+assigned[t]*256;
            if(score<best){best=score;target=t;}
        }
        // Far harvesting while no enemy army has been seen for ~1100 frames
        // (vector[47] is the age of the last sighting): unescorted workers
        // far from the towers otherwise die and bait the army out. With no
        // income at all (vector[9]), idle workers are worth nothing either,
        // so the trip is taken anyway; the per-tile danger checks below still
        // keep them away from enemy buildings, visible forces and raid sites.
        const bool no_income=v.input.vector[9]<0.05f;
        if(target>=o.tiles.size()&&!windows.empty()&&(v.input.vector[47]>=0.5f||no_income)) {
            if(!far_tiles_ready) {
                far_tiles_ready=true;
                for(std::size_t t=0;t<o.tiles.size();++t) {
                    if(!o.tiles[t].resource_amount||!o.tiles[t].explored||claimed.count(t))continue;
                    CommanderPoint p{i32(t%o.map_width_tiles)*32+16,i32(t/o.map_width_tiles)*32+16,true};
                    bool reachable=false;for(const auto& w:windows)if(near(p,w.first,768)){reachable=true;break;}
                    if(!reachable)continue;
                    // Never send unescorted workers toward known enemy
                    // buildings, a visible enemy force or a recent raid site.
                    bool dangerous=false;
                    for(const auto& g:v.enemies)if(building(g.seen)&&near(point(g.seen),p,800)){dangerous=true;break;}
                    if(!dangerous)for(const auto& e:v.visible_enemies)if(combat(e)&&near(point(e),p,640)){dangerous=true;break;}
                    if(!dangerous&&s.threat.valid&&f-s.threat_frame<2200&&near(s.threat,p,640))dangerous=true;
                    if(!dangerous)far_tiles.push_back(t);
                }
            }
            for(auto t:far_tiles) {
                if(assigned[t]>=3)continue;
                CommanderPoint p{i32(t%o.map_width_tiles)*32+16,i32(t/o.map_width_tiles)*32+16,true};
                float trip=std::numeric_limits<float>::max();for(const auto& w:windows)trip=std::min(trip,distance(p,w.first));
                const float score=distance(point(u),p)+2*trip+assigned[t]*256;
                if(score<best){best=score;target=t;}
            }
        }
        if(target<o.tiles.size()) {
            auto a=order(AiSemanticActionKind::harvest,u.id,{i32(target%o.map_width_tiles)*32,i32(target/o.map_width_tiles)*32,true});
            if(publish(a)){state.harvest_tile=i32(target);++assigned[target];}
        }
    }
    // Rally once per producer when H7 changes (and once for a new producer).
    for(const auto& u:v.own)if((u.type_id==0x80||u.type_id==0x84||u.type_id==0x87)&&!u.under_construction&&!commanded.count(u.id)) {
        auto& state=s.units.at(u.id);const auto& sq=v.squads[s.rally_squad];
        auto p=sq.center.valid?sq.center:v.anchors[s.squads[s.rally_squad].anchor];if(!p.valid)p=v.anchors[0];
        const u32 marker=0x10000000u+s.rally_squad+1;
        if(state.applied_intent_serial!=marker) {auto a=order(AiSemanticActionKind::set_rally,u.id,p);if(publish(a))state.applied_intent_serial=marker;}
    }
    std::array<std::vector<u32>,3> members;
    for(const auto& u:v.own)if(combat(u)&&!u.under_construction)members[std::min<u8>(s.units.at(u.id).squad,2)].push_back(u.id);
    for(u32 q=0;q<3;++q) {
        auto& rule=s.squads[q];const auto& ids=members[q];const auto center=v.squads[q].center.valid?v.squads[q].center:v.anchors[0];
        const u32 side=std::max(1u,u32(std::ceil(std::sqrt(float(ids.size())))));u32 scout=0;
        if(rule.intent==CommanderIntent::scout)for(u32 id:ids)if(!scout||speed(*find_unit(v.own,id))>speed(*find_unit(v.own,scout)))scout=id;
        std::vector<CommanderPoint> squad_route;bool squad_route_ready=false;
        // Engage reflex: a visible enemy force near the squad pulls every
        // idle member into one attack-move on its centroid. Otherwise only
        // the members inside the engine's short auto-target radius fight
        // while the rest of the squad stands idle a few tiles away.
        CommanderPoint engage;
        // Holding squads measure from their post, not from the drifting
        // squad center, so a lone scout cannot draw them off a choke.
        const bool posted=rule.intent==CommanderIntent::hold||rule.intent==CommanderIntent::defend;
        const CommanderPoint post=posted&&v.anchors[rule.anchor].valid?v.anchors[rule.anchor]:center;
        if(rule.intent!=CommanderIntent::retreat&&rule.intent!=CommanderIntent::scout&&rule.intent!=CommanderIntent::hunt) {
            i64 ex=0,ey=0;u32 n=0;
            for(const auto& e:v.visible_enemies)if(combat(e)&&near(point(e),post,posted?448:512)){ex+=e.x;ey+=e.y;++n;}
            if(n)engage={i32(ex/n),i32(ey/n),true};
        }
        for(std::size_t index=0;index<ids.size();++index) {
            const u32 id=ids[index];const auto& u=*find_unit(v.own,id);auto& us=s.units.at(id);
            if(commanded.count(id)||merging.count(id)||busy(u))continue;
            if(rule.intent==CommanderIntent::scout&&id!=scout)continue;
            const bool hunting=rule.intent==CommanderIntent::hunt;
            if(us.hunting!=hunting) {
                auto marker=order(AiSemanticActionKind::set_hunt_marker,id);marker.stance_on=hunting;
                if(publish(marker,false))us.hunting=hunting;
            }
            const bool fighting=(u.command_flags&0x10)!=0;
            if(o.research_order_levels[0x38]) {
                const auto* opponent=find_unit(v.visible_enemies,u.target_id);
                const bool engaging=fighting||(opponent&&near(point(u),point(*opponent),i32(u.attack_range)+64));
                const bool active=(u.command_flags&0x10000)!=0;
                const bool want=engaging&&u.action_mode>=2&&rule.roe!=CommanderRoe::cautious;
                if(active!=want){auto a=order(AiSemanticActionKind::set_stance,id);a.stance_id=2;a.stance_on=want;if(publish(a,false))continue;}
            }
            if(fighting)continue; // Finish the current swing before changing orders.
            if(rule.roe!=CommanderRoe::aggressive&&hp(u)<0.25f){issue_point(u,AiSemanticActionKind::move,v.anchors[0]);continue;}
            if(rule.intent==CommanderIntent::retreat){if(issue_point(u,AiSemanticActionKind::move,v.anchors[rule.anchor],us.applied_intent_serial!=rule.serial))us.applied_intent_serial=rule.serial;continue;}
            // Leash: a posted unit that chased beyond its post comes back
            // unless an enemy is right next to it. Faster raiders otherwise
            // kite the squad across the map and kill it one unit at a time.
            if(posted&&distance(point(u),post)>384) {
                bool close=false;for(const auto& e:v.visible_enemies)if(combat(e)&&near(point(e),point(u),160)){close=true;break;}
                if(!close){if(issue_point(u,AiSemanticActionKind::move,post,f-us.last_order_frame>=32))continue;}
            }
            const CommanderPoint gather=posted?post:center;
            // SCOUT deliberately moves only one member while the other
            // members wait. Cohesion must not pull that member back.
            if(rule.intent==CommanderIntent::scout)us.regrouping=false;
            else {
                if(distance(point(u),gather)>384&&!engage.valid)us.regrouping=true;
                if(distance(point(u),gather)<192||engage.valid)us.regrouping=false;
            }
            if(us.regrouping){issue_point(u,AiSemanticActionKind::move,gather);continue;}
            if(engage.valid&&!find_unit(v.visible_enemies,u.target_id)&&u.attack_range_base<=96) {
                CommanderPoint ep{engage.x+(i32(index%side)-i32(side/2))*24,engage.y+(i32(index/side)-i32(side/2))*24,true};
                const CommanderPoint last{us.last_order.target_x,us.last_order.target_y,true};
                const bool moved=us.last_order.kind!=AiSemanticActionKind::attack_move||distance(last,ep)>96;
                if(issue_point(u,AiSemanticActionKind::attack_move,ep,moved)) {
                    // This reflex implements the current intent. Without the
                    // receipt, next tick's unchanged-order fast path fell
                    // through and reissued the original home/route anchor.
                    us.applied_intent_serial=rule.serial;continue;
                }
            }
            const AiObservedUnit* target=nullptr;float best=std::numeric_limits<float>::max();
            const bool ranged=u.attack_range_base>96;
            for(const auto& enemy:v.visible_enemies) {
                if(rule.intent==CommanderIntent::scout)break;
                if(enemy.render_class>=32||(u.attackable_class_mask&(1u<<enemy.render_class))==0)continue;
                if(rule.intent==CommanderIntent::siege&&!building(enemy))continue;
                if(rule.intent==CommanderIntent::harass&&!worker(enemy))continue;
                const float d=distance(point(u),point(enemy));const float range=float(enemy.render_class==3?u.attack_range_vs_air:u.attack_range);
                if(rule.intent!=CommanderIntent::siege&&d>range)continue;
                const float priority=building(enemy)?60:enemy.transport_capacity?50:40;
                const float score=rule.intent==CommanderIntent::siege?d:enemy.health*priority;
                if(score<best){best=score;target=&enemy;}
            }
            if(rule.intent==CommanderIntent::hunt) {
                for(const auto& neutral:v.visible_neutrals)if(neutral.attack_power==0&&v.squads[q].weight>=1.5f*weight(neutral)&&distance(point(u),point(neutral))<best){best=distance(point(u),point(neutral));target=&neutral;}
            }
            if(target&&(ranged||rule.intent==CommanderIntent::hunt)) {
                const auto* current=find_unit(v.visible_enemies,u.target_id);
                if(current&&target->id!=current->id&&current->health<=target->health*1.5f&&rule.intent!=CommanderIntent::siege)target=current;
                const auto range=target->render_class==3?u.attack_range_vs_air:u.attack_range;
                if(rule.intent==CommanderIntent::siege&&distance(point(u),point(*target))>float(range)) {
                    const float d=std::max(1.0f,distance(point(u),point(*target))),offset=std::max(0.0f,float(range)-16);
                    CommanderPoint p{target->x+i32((u.x-target->x)*offset/d),target->y+i32((u.y-target->y)*offset/d),true};
                    issue_point(u,AiSemanticActionKind::move,p);continue;
                }
                bool kited=false;
                if(ranged&&u.command_lockout_ticks>0)for(const auto& e:v.visible_enemies)if(combat(e)&&e.attack_range_base<=96&&near(point(u),point(e),64)&&speed(u)>speed(e)) {
                    const float d=std::max(1.0f,distance(point(u),point(e)));CommanderPoint p{u.x+i32((u.x-e.x)*64/d),u.y+i32((u.y-e.y)*64/d),true};
                    kited=issue_point(u,AiSemanticActionKind::move,p);if(kited)break;
                }
                if(kited)continue;
                if((f-1)%16==0||u.command_state==1||u.target_id==0) {
                    auto a=order(rule.intent==CommanderIntent::hunt?AiSemanticActionKind::hunt_unit:AiSemanticActionKind::attack_unit,id);a.target_unit_id=target->id;
                    if(u.target_id!=target->id||u.command_state==1)if(publish(a)){us.applied_intent_serial=rule.serial;continue;}
                }
                if(fighting)continue;
            }
            if(fighting)continue; // Do not interrupt an attack animation.
            auto p=v.anchors[rule.anchor];if(!p.valid)p=v.anchors[0];
            if(rule.intent==CommanderIntent::siege&&!ranged&&target) {
                const auto d=std::max(1.0f,distance(point(*target),center));p={target->x+i32((center.x-target->x)*128/d),target->y+i32((center.y-target->y)*128/d),true};
            }
            const bool changed=us.applied_intent_serial!=rule.serial;
            const bool stalled=f-us.last_progress>=64&&f-us.last_order_frame>=64;
            if(!changed&&u.command_state!=1&&!stalled)continue;
            if(rule.intent==CommanderIntent::hold&&near(point(u),p,96)&&!changed)continue;
            const CommanderPoint base_target=p;
            p.x+=(i32(index%side)-i32(side/2))*32;p.y+=(i32(index/side)-i32(side/2))*32;
            if(stalled){p.x+=(id&1)?64:-64;p.y+=(id&2)?64:-64;}
            // Long ground marches follow a BFS route (through ramps) computed
            // once per squad per tick; the engine's bounded pathfinder can
            // otherwise give up on cross-map targets and strand the unit.
            if(u.render_class!=3&&distance(point(u),base_target)>640) {
                if(!squad_route_ready){squad_route=path(o,center,base_target);squad_route_ready=true;}
                if(!squad_route.empty()) {
                    std::size_t nearest=0;float d=std::numeric_limits<float>::max();
                    for(std::size_t i=0;i<squad_route.size();++i){const float n=distance(point(u),squad_route[i]);if(n<d){d=n;nearest=i;}}
                    p=squad_route[std::min(squad_route.size()-1,nearest+10)];
                    if(stalled){p.x+=(id&1)?32:-32;p.y+=(id&2)?32:-32;}
                }
            }
            const auto kind=rule.intent==CommanderIntent::scout?AiSemanticActionKind::move:AiSemanticActionKind::attack_move;
            if(issue_point(u,kind,p,changed||stalled))us.applied_intent_serial=rule.serial;
        }
    }
    return output;
}

} // namespace ranker
