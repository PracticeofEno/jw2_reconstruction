#include "ranker_mfc_runtime.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ranker {

namespace {

void validate_nonnegative(int value, const char* name) {
    if (value < 0) {
        throw std::out_of_range(name);
    }
}

template <typename Array, typename Value>
Array& array_set_size(Array& array, int new_size, int grow_by) {
    validate_nonnegative(new_size, "new_size");
    array.grow_by = grow_by;
    array.values.resize(static_cast<std::size_t>(new_size));
    return array;
}

template <typename Array>
int array_append(Array& destination, const Array& source) {
    const int old_size = static_cast<int>(destination.values.size());
    destination.values.insert(destination.values.end(), source.values.begin(),
        source.values.end());
    return old_size;
}

template <typename Array>
Array& array_copy(Array& destination, const Array& source) {
    if (&destination != &source) {
        destination.values = source.values;
        destination.grow_by = source.grow_by;
    }
    return destination;
}

template <typename Array>
Array& array_free_extra(Array& array) {
    Array compact;
    compact.values = array.values;
    compact.grow_by = array.grow_by;
    array = std::move(compact);
    return array;
}

template <typename Array, typename Value>
Array& array_set_at_grow(Array& array, int index, Value value) {
    validate_nonnegative(index, "index");
    const std::size_t offset = static_cast<std::size_t>(index);
    if (offset >= array.values.size()) {
        array.values.resize(offset + 1);
    }
    array.values[offset] = value;
    return array;
}

template <typename Array, typename Value>
Array& array_insert_at(Array& array, int index, Value value, int count) {
    validate_nonnegative(index, "index");
    validate_nonnegative(count, "count");
    const std::size_t offset = std::min<std::size_t>(
        static_cast<std::size_t>(index), array.values.size());
    array.values.insert(array.values.begin() + offset,
        static_cast<std::size_t>(count), value);
    return array;
}

template <typename Array>
Array& array_remove_at(Array& array, int index, int count) {
    validate_nonnegative(index, "index");
    validate_nonnegative(count, "count");
    const std::size_t offset = static_cast<std::size_t>(index);
    if (offset >= array.values.size() || count == 0) {
        return array;
    }
    const std::size_t end = std::min<std::size_t>(array.values.size(),
        offset + static_cast<std::size_t>(count));
    array.values.erase(array.values.begin() + offset, array.values.begin() + end);
    return array;
}

template <typename Array>
Array& array_insert_array_at(Array& array, int index, const Array& source) {
    validate_nonnegative(index, "index");
    const std::size_t offset = std::min<std::size_t>(
        static_cast<std::size_t>(index), array.values.size());
    array.values.insert(array.values.begin() + offset, source.values.begin(),
        source.values.end());
    return array;
}

template <typename Array>
void array_assert_valid(const Array& array) {
    if (array.grow_by < 0) {
        throw std::logic_error("negative grow_by");
    }
}

template <typename Array>
Array* array_scalar_dtor(Array* array, unsigned flags) {
    if (array == nullptr) {
        return nullptr;
    }
    array->values.clear();
    if ((flags & 1U) != 0U) {
        delete array;
    }
    return array;
}

template <typename Array>
int array_get_size(const Array& array) {
    return static_cast<int>(array.values.size());
}

template <typename Array>
int array_get_upper_bound(const Array& array) {
    return static_cast<int>(array.values.size()) - 1;
}

template <typename Array>
void array_remove_all(Array& array) {
    array.values.clear();
}

template <typename Array>
std::size_t array_checked_offset(const Array& array, int index,
    const char* name) {
    validate_nonnegative(index, name);
    const std::size_t offset = static_cast<std::size_t>(index);
    if (offset >= array.values.size()) {
        throw std::out_of_range(name);
    }
    return offset;
}

template <typename Array>
auto& array_checked_ref(Array& array, int index, const char* name) {
    return array.values[array_checked_offset(array, index, name)];
}

template <typename Array>
const auto& array_checked_ref(const Array& array, int index,
    const char* name) {
    return array.values[array_checked_offset(array, index, name)];
}

template <typename Array>
auto array_data(Array& array) -> decltype(array.values.data()) {
    return array.values.empty() ? nullptr : array.values.data();
}

template <typename Array>
auto array_data(const Array& array) -> decltype(array.values.data()) {
    return array.values.empty() ? nullptr : array.values.data();
}

template <typename Map>
int map_get_count(const Map& map) {
    return static_cast<int>(map.entries.size());
}

template <typename Map>
bool map_is_empty(const Map& map) {
    return map.entries.empty();
}

template <typename Map>
void* map_start_position_sentinel(const Map& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return reinterpret_cast<void*>(~std::uintptr_t{0});
}

template <typename Map>
int map_hash_table_size(const Map& map) {
    return map.hash_table_size;
}

MfcPtrListNodeCompat* make_node(void* value,
    MfcPtrListNodeCompat* previous, MfcPtrListNodeCompat* next) {
    auto* node = new MfcPtrListNodeCompat;
    node->value = value;
    node->previous = previous;
    node->next = next;
    return node;
}

MfcCStringListNodeCompat* make_string_node(const MfcCStringCompat& value,
    MfcCStringListNodeCompat* previous, MfcCStringListNodeCompat* next) {
    auto* node = new MfcCStringListNodeCompat;
    node->value = value;
    node->previous = previous;
    node->next = next;
    return node;
}

MfcPtrListNodeCompat* require_ptr_list_node(MfcPtrListNodeCompat* node,
    const char* name) {
    if (node == nullptr) {
        throw std::out_of_range(name);
    }
    return node;
}

MfcCStringListNodeCompat* require_string_list_node(
    MfcCStringListNodeCompat* node, const char* name) {
    if (node == nullptr) {
        throw std::out_of_range(name);
    }
    return node;
}

} // namespace

void* PtrListGetPlexData(MfcPtrListCompat& list) {
    return list.head;
}

MfcPtrListCompat& ConstructPtrList(MfcPtrListCompat& list, int block_size) {
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.block_size = block_size > 0 ? block_size : 10;
    return list;
}

void PtrListRemoveAll(MfcPtrListCompat& list) {
    for (MfcPtrListNodeCompat* node = list.head; node != nullptr;) {
        MfcPtrListNodeCompat* next = node->next;
        delete node;
        node = next;
    }
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
}

void DestructPtrList(MfcPtrListCompat& list) {
    PtrListRemoveAll(list);
}

MfcPtrListNodeCompat* PtrListNewNode(MfcPtrListCompat& list,
    MfcPtrListNodeCompat* previous, MfcPtrListNodeCompat* next) {
    MfcPtrListNodeCompat* node = make_node(nullptr, previous, next);
    if (previous != nullptr) {
        previous->next = node;
    } else {
        list.head = node;
    }
    if (next != nullptr) {
        next->previous = node;
    } else {
        list.tail = node;
    }
    ++list.count;
    return node;
}

void PtrListFreeNode(MfcPtrListCompat& list, MfcPtrListNodeCompat* node) {
    if (node == nullptr) {
        return;
    }
    if (node->previous != nullptr) {
        node->previous->next = node->next;
    } else {
        list.head = node->next;
    }
    if (node->next != nullptr) {
        node->next->previous = node->previous;
    } else {
        list.tail = node->previous;
    }
    delete node;
    --list.count;
}

MfcPtrListNodeCompat* PtrListAddHead(MfcPtrListCompat& list, void* value) {
    MfcPtrListNodeCompat* node = PtrListNewNode(list, nullptr, list.head);
    node->value = value;
    return node;
}

MfcPtrListNodeCompat* PtrListAddTail(MfcPtrListCompat& list, void* value) {
    MfcPtrListNodeCompat* node = PtrListNewNode(list, list.tail, nullptr);
    node->value = value;
    return node;
}

void PtrListAddHeadList(MfcPtrListCompat& list, const MfcPtrListCompat& source) {
    for (MfcPtrListNodeCompat* node = source.tail; node != nullptr;
         node = node->previous) {
        PtrListAddHead(list, node->value);
    }
}

void PtrListAddTailList(MfcPtrListCompat& list, const MfcPtrListCompat& source) {
    for (MfcPtrListNodeCompat* node = source.head; node != nullptr;
         node = node->next) {
        PtrListAddTail(list, node->value);
    }
}

void* PtrListRemoveHead(MfcPtrListCompat& list) {
    if (list.head == nullptr) {
        throw std::out_of_range("empty list");
    }
    void* value = list.head->value;
    PtrListFreeNode(list, list.head);
    return value;
}

void* PtrListRemoveTail(MfcPtrListCompat& list) {
    if (list.tail == nullptr) {
        throw std::out_of_range("empty list");
    }
    void* value = list.tail->value;
    PtrListFreeNode(list, list.tail);
    return value;
}

MfcPtrListNodeCompat* PtrListInsertBefore(MfcPtrListCompat& list,
    MfcPtrListNodeCompat* position, void* value) {
    if (position == nullptr) {
        return PtrListAddHead(list, value);
    }
    MfcPtrListNodeCompat* node =
        PtrListNewNode(list, position->previous, position);
    node->value = value;
    return node;
}

MfcPtrListNodeCompat* PtrListInsertAfter(MfcPtrListCompat& list,
    MfcPtrListNodeCompat* position, void* value) {
    if (position == nullptr) {
        return PtrListAddTail(list, value);
    }
    MfcPtrListNodeCompat* node =
        PtrListNewNode(list, position, position->next);
    node->value = value;
    return node;
}

void PtrListRemoveAt(MfcPtrListCompat& list, MfcPtrListNodeCompat* position) {
    PtrListFreeNode(list, position);
}

MfcPtrListNodeCompat* PtrListFindIndex(MfcPtrListCompat& list, int index) {
    if (index < 0 || index >= list.count) {
        return nullptr;
    }
    MfcPtrListNodeCompat* node = list.head;
    for (int i = 0; i < index && node != nullptr; ++i) {
        node = node->next;
    }
    return node;
}

MfcPtrListNodeCompat* PtrListFindValue(MfcPtrListCompat& list, void* value,
    MfcPtrListNodeCompat* start_after) {
    MfcPtrListNodeCompat* node =
        start_after != nullptr ? start_after->next : list.head;
    for (; node != nullptr; node = node->next) {
        if (node->value == value) {
            return node;
        }
    }
    return nullptr;
}

void PtrListDump(const MfcPtrListCompat& list) {
    AfxTraceOutput("CPtrList count=%d\n", list.count);
}

void PtrListAssertValid(const MfcPtrListCompat& list) {
    int forward_count = 0;
    const MfcPtrListNodeCompat* previous = nullptr;
    for (const MfcPtrListNodeCompat* node = list.head; node != nullptr;
         node = node->next) {
        if (node->previous != previous) {
            throw std::logic_error("broken CPtrList links");
        }
        previous = node;
        ++forward_count;
    }
    if (forward_count != list.count || previous != list.tail) {
        throw std::logic_error("invalid CPtrList count");
    }
}

MfcPtrListCompat* DeletePtrListScalarDtor(MfcPtrListCompat* list,
    unsigned flags) {
    if (list == nullptr) {
        return nullptr;
    }
    DestructPtrList(*list);
    if ((flags & 1U) != 0U) {
        delete list;
    }
    return list;
}

int PtrListGetCount(const MfcPtrListCompat& list) {
    return list.count;
}

bool PtrListIsEmpty(const MfcPtrListCompat& list) {
    return list.count == 0;
}

void*& PtrListGetHeadRef(MfcPtrListCompat& list) {
    return require_ptr_list_node(list.head, "PtrListGetHeadRef")->value;
}

void* PtrListGetHead(const MfcPtrListCompat& list) {
    return require_ptr_list_node(list.head, "PtrListGetHead")->value;
}

void*& PtrListGetTailRef(MfcPtrListCompat& list) {
    return require_ptr_list_node(list.tail, "PtrListGetTailRef")->value;
}

void* PtrListGetTail(const MfcPtrListCompat& list) {
    return require_ptr_list_node(list.tail, "PtrListGetTail")->value;
}

MfcPtrListNodeCompat* PtrListGetHeadPosition(const MfcPtrListCompat& list) {
    return list.head;
}

MfcPtrListNodeCompat* PtrListGetTailPosition(const MfcPtrListCompat& list) {
    return list.tail;
}

void*& PtrListGetNextRef(MfcPtrListNodeCompat*& position) {
    MfcPtrListNodeCompat* node =
        require_ptr_list_node(position, "PtrListGetNextRef");
    position = node->next;
    return node->value;
}

void* PtrListGetNext(MfcPtrListNodeCompat*& position) {
    MfcPtrListNodeCompat* node =
        require_ptr_list_node(position, "PtrListGetNext");
    position = node->next;
    return node->value;
}

void*& PtrListGetPrevRef(MfcPtrListNodeCompat*& position) {
    MfcPtrListNodeCompat* node =
        require_ptr_list_node(position, "PtrListGetPrevRef");
    position = node->previous;
    return node->value;
}

void* PtrListGetPrev(MfcPtrListNodeCompat*& position) {
    MfcPtrListNodeCompat* node =
        require_ptr_list_node(position, "PtrListGetPrev");
    position = node->previous;
    return node->value;
}

void*& PtrListGetAtRef(MfcPtrListNodeCompat* position) {
    return require_ptr_list_node(position, "PtrListGetAtRef")->value;
}

void* PtrListGetAt(MfcPtrListNodeCompat* position) {
    return require_ptr_list_node(position, "PtrListGetAt")->value;
}

void PtrListSetAt(MfcPtrListNodeCompat* position, void* value) {
    require_ptr_list_node(position, "PtrListSetAt")->value = value;
}

MfcPtrListNodeCompat* PtrListFind(MfcPtrListCompat& list, void* value,
    MfcPtrListNodeCompat* start_after) {
    return PtrListFindValue(list, value, start_after);
}

int ObListGetCount(const MfcObListCompat& list) {
    return PtrListGetCount(list);
}

bool ObListIsEmpty(const MfcObListCompat& list) {
    return PtrListIsEmpty(list);
}

void*& ObListGetHeadRef(MfcObListCompat& list) {
    return PtrListGetHeadRef(list);
}

void* ObListGetHead(const MfcObListCompat& list) {
    return PtrListGetHead(list);
}

void*& ObListGetTailRef(MfcObListCompat& list) {
    return PtrListGetTailRef(list);
}

void* ObListGetTail(const MfcObListCompat& list) {
    return PtrListGetTail(list);
}

MfcObListNodeCompat* ObListGetHeadPosition(const MfcObListCompat& list) {
    return PtrListGetHeadPosition(list);
}

MfcObListNodeCompat* ObListGetTailPosition(const MfcObListCompat& list) {
    return PtrListGetTailPosition(list);
}

void*& ObListGetNextRef(MfcObListNodeCompat*& position) {
    return PtrListGetNextRef(position);
}

void* ObListGetNext(MfcObListNodeCompat*& position) {
    return PtrListGetNext(position);
}

void*& ObListGetPrevRef(MfcObListNodeCompat*& position) {
    return PtrListGetPrevRef(position);
}

void* ObListGetPrev(MfcObListNodeCompat*& position) {
    return PtrListGetPrev(position);
}

void*& ObListGetAtRef(MfcObListNodeCompat* position) {
    return PtrListGetAtRef(position);
}

void* ObListGetAt(MfcObListNodeCompat* position) {
    return PtrListGetAt(position);
}

void ObListSetAt(MfcObListNodeCompat* position, void* value) {
    PtrListSetAt(position, value);
}

MfcCStringListCompat& ConstructCStringList(MfcCStringListCompat& list,
    int block_size) {
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.block_size = block_size > 0 ? block_size : 10;
    return list;
}

void CStringListRemoveAll(MfcCStringListCompat& list) {
    for (MfcCStringListNodeCompat* node = list.head; node != nullptr;) {
        MfcCStringListNodeCompat* next = node->next;
        delete node;
        node = next;
    }
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
}

void DestructCStringList(MfcCStringListCompat& list) {
    CStringListRemoveAll(list);
}

MfcCStringListNodeCompat* CStringListAddHead(MfcCStringListCompat& list,
    const MfcCStringCompat& value) {
    MfcCStringListNodeCompat* node =
        make_string_node(value, nullptr, list.head);
    if (list.head != nullptr) {
        list.head->previous = node;
    } else {
        list.tail = node;
    }
    list.head = node;
    ++list.count;
    return node;
}

MfcCStringListNodeCompat* CStringListAddTail(MfcCStringListCompat& list,
    const MfcCStringCompat& value) {
    MfcCStringListNodeCompat* node =
        make_string_node(value, list.tail, nullptr);
    if (list.tail != nullptr) {
        list.tail->next = node;
    } else {
        list.head = node;
    }
    list.tail = node;
    ++list.count;
    return node;
}

MfcCStringListNodeCompat* CStringListAddHeadText(
    MfcCStringListCompat& list, const char* text) {
    MfcCStringCompat value;
    value.text = text != nullptr ? text : "";
    return CStringListAddHead(list, value);
}

MfcCStringListNodeCompat* CStringListAddTailText(
    MfcCStringListCompat& list, const char* text) {
    MfcCStringCompat value;
    value.text = text != nullptr ? text : "";
    return CStringListAddTail(list, value);
}

int CStringListGetCount(const MfcCStringListCompat& list) {
    return list.count;
}

bool CStringListIsEmpty(const MfcCStringListCompat& list) {
    return list.count == 0;
}

MfcCStringCompat& CStringListGetHeadRef(MfcCStringListCompat& list) {
    return require_string_list_node(list.head, "CStringListGetHeadRef")->value;
}

MfcCStringCompat CStringListGetHead(const MfcCStringListCompat& list) {
    return require_string_list_node(list.head, "CStringListGetHead")->value;
}

MfcCStringCompat& CStringListGetTailRef(MfcCStringListCompat& list) {
    return require_string_list_node(list.tail, "CStringListGetTailRef")->value;
}

MfcCStringCompat CStringListGetTail(const MfcCStringListCompat& list) {
    return require_string_list_node(list.tail, "CStringListGetTail")->value;
}

MfcCStringListNodeCompat* CStringListGetHeadPosition(
    const MfcCStringListCompat& list) {
    return list.head;
}

MfcCStringListNodeCompat* CStringListGetTailPosition(
    const MfcCStringListCompat& list) {
    return list.tail;
}

MfcCStringCompat& CStringListGetNextRef(
    MfcCStringListNodeCompat*& position) {
    MfcCStringListNodeCompat* node =
        require_string_list_node(position, "CStringListGetNextRef");
    position = node->next;
    return node->value;
}

MfcCStringCompat CStringListGetNext(MfcCStringListNodeCompat*& position) {
    MfcCStringListNodeCompat* node =
        require_string_list_node(position, "CStringListGetNext");
    position = node->next;
    return node->value;
}

MfcCStringCompat& CStringListGetPrevRef(
    MfcCStringListNodeCompat*& position) {
    MfcCStringListNodeCompat* node =
        require_string_list_node(position, "CStringListGetPrevRef");
    position = node->previous;
    return node->value;
}

MfcCStringCompat CStringListGetPrev(MfcCStringListNodeCompat*& position) {
    MfcCStringListNodeCompat* node =
        require_string_list_node(position, "CStringListGetPrev");
    position = node->previous;
    return node->value;
}

MfcCStringCompat& CStringListGetAtRef(MfcCStringListNodeCompat* position) {
    return require_string_list_node(position, "CStringListGetAtRef")->value;
}

MfcCStringCompat CStringListGetAt(MfcCStringListNodeCompat* position) {
    return require_string_list_node(position, "CStringListGetAt")->value;
}

void CStringListSetAtText(MfcCStringListNodeCompat* position,
    const char* text) {
    require_string_list_node(position, "CStringListSetAtText")->value.text =
        text != nullptr ? text : "";
}

void CStringListSetAtString(MfcCStringListNodeCompat* position,
    const MfcCStringCompat& value) {
    require_string_list_node(position, "CStringListSetAtString")->value =
        value;
}

int ByteArrayGetSize(const MfcByteArrayCompat& array) {
    return array_get_size(array);
}

int ByteArrayGetUpperBound(const MfcByteArrayCompat& array) {
    return array_get_upper_bound(array);
}

void ByteArrayRemoveAll(MfcByteArrayCompat& array) {
    array_remove_all(array);
}

unsigned char ByteArrayGetAt(const MfcByteArrayCompat& array, int index) {
    return array_checked_ref(array, index, "ByteArrayGetAt");
}

void ByteArraySetAt(MfcByteArrayCompat& array, int index,
    unsigned char value) {
    array_checked_ref(array, index, "ByteArraySetAt") = value;
}

unsigned char& ByteArrayElementAt(MfcByteArrayCompat& array, int index) {
    return array_checked_ref(array, index, "ByteArrayElementAt");
}

unsigned char* ByteArrayGetData(MfcByteArrayCompat& array) {
    return array_data(array);
}

const unsigned char* ByteArrayGetDataConst(
    const MfcByteArrayCompat& array) {
    return array_data(array);
}

unsigned char ByteArraySubscript(const MfcByteArrayCompat& array, int index) {
    return ByteArrayGetAt(array, index);
}

unsigned char& ByteArraySubscriptRef(MfcByteArrayCompat& array, int index) {
    return ByteArrayElementAt(array, index);
}

MfcByteArrayCompat& ByteArraySetSize(MfcByteArrayCompat& array,
    int new_size, int grow_by) {
    return array_set_size<MfcByteArrayCompat, unsigned char>(array, new_size,
        grow_by);
}

int ByteArrayAppend(MfcByteArrayCompat& destination,
    const MfcByteArrayCompat& source) {
    return array_append(destination, source);
}

MfcByteArrayCompat& ByteArrayCopy(MfcByteArrayCompat& destination,
    const MfcByteArrayCompat& source) {
    return array_copy(destination, source);
}

MfcByteArrayCompat& ByteArrayFreeExtra(MfcByteArrayCompat& array) {
    return array_free_extra(array);
}

MfcByteArrayCompat& ByteArraySetAtGrow(MfcByteArrayCompat& array,
    int index, unsigned char value) {
    return array_set_at_grow(array, index, value);
}

MfcByteArrayCompat& ByteArrayInsertAt(MfcByteArrayCompat& array, int index,
    unsigned char value, int count) {
    return array_insert_at(array, index, value, count);
}

MfcByteArrayCompat& ByteArrayRemoveAt(MfcByteArrayCompat& array, int index,
    int count) {
    return array_remove_at(array, index, count);
}

MfcByteArrayCompat& ByteArrayInsertArrayAt(MfcByteArrayCompat& array,
    int index, const MfcByteArrayCompat& source) {
    return array_insert_array_at(array, index, source);
}

void ByteArraySerialize(MfcByteArrayCompat& array, void* archive) {
    (void)array;
    (void)archive;
}

void ByteArrayAssertValid(const MfcByteArrayCompat& array) {
    array_assert_valid(array);
}

MfcByteArrayCompat* DeleteByteArrayScalarDtor(MfcByteArrayCompat* array,
    unsigned flags) {
    return array_scalar_dtor(array, flags);
}

int WordArrayGetSize(const MfcWordArrayCompat& array) {
    return array_get_size(array);
}

int WordArrayGetUpperBound(const MfcWordArrayCompat& array) {
    return array_get_upper_bound(array);
}

void WordArrayRemoveAll(MfcWordArrayCompat& array) {
    array_remove_all(array);
}

unsigned short WordArrayGetAt(const MfcWordArrayCompat& array, int index) {
    return array_checked_ref(array, index, "WordArrayGetAt");
}

void WordArraySetAt(MfcWordArrayCompat& array, int index,
    unsigned short value) {
    array_checked_ref(array, index, "WordArraySetAt") = value;
}

unsigned short& WordArrayElementAt(MfcWordArrayCompat& array, int index) {
    return array_checked_ref(array, index, "WordArrayElementAt");
}

unsigned short* WordArrayGetData(MfcWordArrayCompat& array) {
    return array_data(array);
}

const unsigned short* WordArrayGetDataConst(
    const MfcWordArrayCompat& array) {
    return array_data(array);
}

unsigned short WordArraySubscript(const MfcWordArrayCompat& array,
    int index) {
    return WordArrayGetAt(array, index);
}

unsigned short& WordArraySubscriptRef(MfcWordArrayCompat& array,
    int index) {
    return WordArrayElementAt(array, index);
}

MfcWordArrayCompat& WordArraySetSize(MfcWordArrayCompat& array,
    int new_size, int grow_by) {
    return array_set_size<MfcWordArrayCompat, unsigned short>(array, new_size,
        grow_by);
}

int WordArrayAppend(MfcWordArrayCompat& destination,
    const MfcWordArrayCompat& source) {
    return array_append(destination, source);
}

MfcWordArrayCompat& WordArrayCopy(MfcWordArrayCompat& destination,
    const MfcWordArrayCompat& source) {
    return array_copy(destination, source);
}

MfcWordArrayCompat& WordArrayFreeExtra(MfcWordArrayCompat& array) {
    return array_free_extra(array);
}

MfcWordArrayCompat& WordArraySetAtGrow(MfcWordArrayCompat& array,
    int index, unsigned short value) {
    return array_set_at_grow(array, index, value);
}

MfcWordArrayCompat& WordArrayInsertAt(MfcWordArrayCompat& array, int index,
    unsigned short value, int count) {
    return array_insert_at(array, index, value, count);
}

MfcWordArrayCompat& WordArrayRemoveAt(MfcWordArrayCompat& array, int index,
    int count) {
    return array_remove_at(array, index, count);
}

MfcWordArrayCompat& WordArrayInsertArrayAt(MfcWordArrayCompat& array,
    int index, const MfcWordArrayCompat& source) {
    return array_insert_array_at(array, index, source);
}

void WordArraySerialize(MfcWordArrayCompat& array, void* archive) {
    (void)array;
    (void)archive;
}

void WordArrayAssertValid(const MfcWordArrayCompat& array) {
    array_assert_valid(array);
}

MfcWordArrayCompat* DeleteWordArrayScalarDtor(MfcWordArrayCompat* array,
    unsigned flags) {
    return array_scalar_dtor(array, flags);
}

int DWordArrayGetSize(const MfcDWordArrayCompat& array) {
    return array_get_size(array);
}

int DWordArrayGetUpperBound(const MfcDWordArrayCompat& array) {
    return array_get_upper_bound(array);
}

void DWordArrayRemoveAll(MfcDWordArrayCompat& array) {
    array_remove_all(array);
}

unsigned long DWordArrayGetAt(const MfcDWordArrayCompat& array, int index) {
    return array_checked_ref(array, index, "DWordArrayGetAt");
}

void DWordArraySetAt(MfcDWordArrayCompat& array, int index,
    unsigned long value) {
    array_checked_ref(array, index, "DWordArraySetAt") = value;
}

unsigned long& DWordArrayElementAt(MfcDWordArrayCompat& array, int index) {
    return array_checked_ref(array, index, "DWordArrayElementAt");
}

unsigned long* DWordArrayGetData(MfcDWordArrayCompat& array) {
    return array_data(array);
}

const unsigned long* DWordArrayGetDataConst(
    const MfcDWordArrayCompat& array) {
    return array_data(array);
}

unsigned long DWordArraySubscript(const MfcDWordArrayCompat& array,
    int index) {
    return DWordArrayGetAt(array, index);
}

unsigned long& DWordArraySubscriptRef(MfcDWordArrayCompat& array,
    int index) {
    return DWordArrayElementAt(array, index);
}

MfcDWordArrayCompat& DWordArraySetSize(MfcDWordArrayCompat& array,
    int new_size, int grow_by) {
    return array_set_size<MfcDWordArrayCompat, unsigned long>(array, new_size,
        grow_by);
}

int DWordArrayAppend(MfcDWordArrayCompat& destination,
    const MfcDWordArrayCompat& source) {
    return array_append(destination, source);
}

MfcDWordArrayCompat& DWordArrayCopy(MfcDWordArrayCompat& destination,
    const MfcDWordArrayCompat& source) {
    return array_copy(destination, source);
}

MfcDWordArrayCompat& DWordArrayFreeExtra(MfcDWordArrayCompat& array) {
    return array_free_extra(array);
}

MfcDWordArrayCompat& DWordArraySetAtGrow(MfcDWordArrayCompat& array,
    int index, unsigned long value) {
    return array_set_at_grow(array, index, value);
}

MfcDWordArrayCompat& DWordArrayInsertAt(MfcDWordArrayCompat& array, int index,
    unsigned long value, int count) {
    return array_insert_at(array, index, value, count);
}

MfcDWordArrayCompat& DWordArrayRemoveAt(MfcDWordArrayCompat& array, int index,
    int count) {
    return array_remove_at(array, index, count);
}

MfcDWordArrayCompat& DWordArrayInsertArrayAt(MfcDWordArrayCompat& array,
    int index, const MfcDWordArrayCompat& source) {
    return array_insert_array_at(array, index, source);
}

void DWordArraySerialize(MfcDWordArrayCompat& array, void* archive) {
    (void)array;
    (void)archive;
}

void DWordArrayAssertValid(const MfcDWordArrayCompat& array) {
    array_assert_valid(array);
}

MfcDWordArrayCompat* DeleteDWordArrayScalarDtor(MfcDWordArrayCompat* array,
    unsigned flags) {
    return array_scalar_dtor(array, flags);
}

int UIntArrayGetSize(const MfcUIntArrayCompat& array) {
    return array_get_size(array);
}

int UIntArrayGetUpperBound(const MfcUIntArrayCompat& array) {
    return array_get_upper_bound(array);
}

void UIntArrayRemoveAll(MfcUIntArrayCompat& array) {
    array_remove_all(array);
}

unsigned int UIntArrayGetAt(const MfcUIntArrayCompat& array, int index) {
    return array_checked_ref(array, index, "UIntArrayGetAt");
}

void UIntArraySetAt(MfcUIntArrayCompat& array, int index,
    unsigned int value) {
    array_checked_ref(array, index, "UIntArraySetAt") = value;
}

unsigned int& UIntArrayElementAt(MfcUIntArrayCompat& array, int index) {
    return array_checked_ref(array, index, "UIntArrayElementAt");
}

unsigned int* UIntArrayGetData(MfcUIntArrayCompat& array) {
    return array_data(array);
}

const unsigned int* UIntArrayGetDataConst(
    const MfcUIntArrayCompat& array) {
    return array_data(array);
}

unsigned int UIntArraySubscript(const MfcUIntArrayCompat& array,
    int index) {
    return UIntArrayGetAt(array, index);
}

unsigned int& UIntArraySubscriptRef(MfcUIntArrayCompat& array, int index) {
    return UIntArrayElementAt(array, index);
}

MfcUIntArrayCompat& UIntArraySetSize(MfcUIntArrayCompat& array,
    int new_size, int grow_by) {
    return array_set_size<MfcUIntArrayCompat, unsigned int>(array, new_size,
        grow_by);
}

int UIntArrayAppend(MfcUIntArrayCompat& destination,
    const MfcUIntArrayCompat& source) {
    return array_append(destination, source);
}

MfcUIntArrayCompat& UIntArrayCopy(MfcUIntArrayCompat& destination,
    const MfcUIntArrayCompat& source) {
    return array_copy(destination, source);
}

MfcUIntArrayCompat& UIntArrayFreeExtra(MfcUIntArrayCompat& array) {
    return array_free_extra(array);
}

MfcUIntArrayCompat& UIntArraySetAtGrow(MfcUIntArrayCompat& array,
    int index, unsigned int value) {
    return array_set_at_grow(array, index, value);
}

MfcUIntArrayCompat& UIntArrayInsertAt(MfcUIntArrayCompat& array, int index,
    unsigned int value, int count) {
    return array_insert_at(array, index, value, count);
}

MfcUIntArrayCompat& UIntArrayRemoveAt(MfcUIntArrayCompat& array, int index,
    int count) {
    return array_remove_at(array, index, count);
}

MfcUIntArrayCompat& UIntArrayInsertArrayAt(MfcUIntArrayCompat& array,
    int index, const MfcUIntArrayCompat& source) {
    return array_insert_array_at(array, index, source);
}

void UIntArrayAssertValid(const MfcUIntArrayCompat& array) {
    array_assert_valid(array);
}

MfcUIntArrayCompat* DeleteUIntArrayScalarDtor(MfcUIntArrayCompat* array,
    unsigned flags) {
    return array_scalar_dtor(array, flags);
}

int PtrArrayGetSize(const MfcPtrArrayCompat& array) {
    return array_get_size(array);
}

int PtrArrayGetUpperBound(const MfcPtrArrayCompat& array) {
    return array_get_upper_bound(array);
}

void PtrArrayRemoveAll(MfcPtrArrayCompat& array) {
    array_remove_all(array);
}

void* PtrArrayGetAt(const MfcPtrArrayCompat& array, int index) {
    return array_checked_ref(array, index, "PtrArrayGetAt");
}

void PtrArraySetAt(MfcPtrArrayCompat& array, int index, void* value) {
    array_checked_ref(array, index, "PtrArraySetAt") = value;
}

void*& PtrArrayElementAt(MfcPtrArrayCompat& array, int index) {
    return array_checked_ref(array, index, "PtrArrayElementAt");
}

void** PtrArrayGetData(MfcPtrArrayCompat& array) {
    return array_data(array);
}

void* const* PtrArrayGetDataConst(const MfcPtrArrayCompat& array) {
    return array_data(array);
}

void* PtrArraySubscript(const MfcPtrArrayCompat& array, int index) {
    return PtrArrayGetAt(array, index);
}

void*& PtrArraySubscriptRef(MfcPtrArrayCompat& array, int index) {
    return PtrArrayElementAt(array, index);
}

MfcPtrArrayCompat& PtrArraySetSize(MfcPtrArrayCompat& array, int new_size,
    int grow_by) {
    return array_set_size<MfcPtrArrayCompat, void*>(array, new_size, grow_by);
}

int PtrArrayAppend(MfcPtrArrayCompat& destination,
    const MfcPtrArrayCompat& source) {
    return array_append(destination, source);
}

MfcPtrArrayCompat& PtrArrayCopy(MfcPtrArrayCompat& destination,
    const MfcPtrArrayCompat& source) {
    return array_copy(destination, source);
}

MfcPtrArrayCompat& PtrArrayFreeExtra(MfcPtrArrayCompat& array) {
    return array_free_extra(array);
}

MfcPtrArrayCompat& PtrArraySetAtGrow(MfcPtrArrayCompat& array, int index,
    void* value) {
    return array_set_at_grow(array, index, value);
}

MfcPtrArrayCompat& PtrArrayInsertAt(MfcPtrArrayCompat& array, int index,
    void* value, int count) {
    return array_insert_at(array, index, value, count);
}

MfcPtrArrayCompat& PtrArrayRemoveAt(MfcPtrArrayCompat& array, int index,
    int count) {
    return array_remove_at(array, index, count);
}

MfcPtrArrayCompat& PtrArrayInsertArrayAt(MfcPtrArrayCompat& array, int index,
    const MfcPtrArrayCompat& source) {
    return array_insert_array_at(array, index, source);
}

void PtrArrayDump(const MfcPtrArrayCompat& array) {
    AfxTraceOutput("CPtrArray count=%zu\n", array.values.size());
}

void PtrArrayAssertValid(const MfcPtrArrayCompat& array) {
    array_assert_valid(array);
}

MfcPtrArrayCompat* DeletePtrArrayScalarDtor(MfcPtrArrayCompat* array,
    unsigned flags) {
    return array_scalar_dtor(array, flags);
}

MfcObArrayCompat& ConstructObArray(MfcObArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

void DestructObArray(MfcObArrayCompat& array) {
    array.values.clear();
}

int ObArrayGetSize(const MfcObArrayCompat& array) {
    return array_get_size(array);
}

int ObArrayGetUpperBound(const MfcObArrayCompat& array) {
    return array_get_upper_bound(array);
}

void ObArrayRemoveAll(MfcObArrayCompat& array) {
    array_remove_all(array);
}

void* ObArrayGetAt(const MfcObArrayCompat& array, int index) {
    return array_checked_ref(array, index, "ObArrayGetAt");
}

void ObArraySetAt(MfcObArrayCompat& array, int index, void* value) {
    array_checked_ref(array, index, "ObArraySetAt") = value;
}

void*& ObArrayElementAt(MfcObArrayCompat& array, int index) {
    return array_checked_ref(array, index, "ObArrayElementAt");
}

void** ObArrayGetData(MfcObArrayCompat& array) {
    return array_data(array);
}

void* const* ObArrayGetDataConst(const MfcObArrayCompat& array) {
    return array_data(array);
}

void* ObArraySubscript(const MfcObArrayCompat& array, int index) {
    return ObArrayGetAt(array, index);
}

void*& ObArraySubscriptRef(MfcObArrayCompat& array, int index) {
    return ObArrayElementAt(array, index);
}

MfcObArrayCompat& ObArraySetSize(MfcObArrayCompat& array, int new_size,
    int grow_by) {
    return array_set_size<MfcObArrayCompat, void*>(array, new_size, grow_by);
}

int ObArrayAppend(MfcObArrayCompat& destination,
    const MfcObArrayCompat& source) {
    return array_append(destination, source);
}

MfcObArrayCompat& ObArrayCopy(MfcObArrayCompat& destination,
    const MfcObArrayCompat& source) {
    return array_copy(destination, source);
}

MfcObArrayCompat& ObArrayFreeExtra(MfcObArrayCompat& array) {
    return array_free_extra(array);
}

MfcObArrayCompat& ObArraySetAtGrow(MfcObArrayCompat& array, int index,
    void* value) {
    return array_set_at_grow(array, index, value);
}

MfcObArrayCompat& ObArrayInsertAt(MfcObArrayCompat& array, int index,
    void* value, int count) {
    return array_insert_at(array, index, value, count);
}

MfcObArrayCompat& ObArrayRemoveAt(MfcObArrayCompat& array, int index,
    int count) {
    return array_remove_at(array, index, count);
}

MfcObArrayCompat& ObArrayInsertArrayAt(MfcObArrayCompat& array, int index,
    const MfcObArrayCompat& source) {
    return array_insert_array_at(array, index, source);
}

void ObArraySerialize(MfcObArrayCompat& array, void* archive) {
    (void)array;
    (void)archive;
}

void ObArrayDump(const MfcObArrayCompat& array) {
    AfxTraceOutput("CObArray count=%zu\n", array.values.size());
}

void ObArrayAssertValid(const MfcObArrayCompat& array) {
    array_assert_valid(array);
}

MfcObArrayCompat* DeleteObArrayScalarDtor(MfcObArrayCompat* array,
    unsigned flags) {
    return array_scalar_dtor(array, flags);
}

MfcCStringArrayCompat& ConstructCStringArray(MfcCStringArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

void DestructCStringArray(MfcCStringArrayCompat& array) {
    array.values.clear();
}

int CStringArrayGetSize(const MfcCStringArrayCompat& array) {
    return array_get_size(array);
}

int CStringArrayGetUpperBound(const MfcCStringArrayCompat& array) {
    return array_get_upper_bound(array);
}

void CStringArrayRemoveAll(MfcCStringArrayCompat& array) {
    array_remove_all(array);
}

MfcCStringCompat CStringArrayGetAt(const MfcCStringArrayCompat& array,
    int index) {
    return array_checked_ref(array, index, "CStringArrayGetAt");
}

void CStringArraySetAtText(MfcCStringArrayCompat& array, int index,
    const char* text) {
    array_checked_ref(array, index, "CStringArraySetAtText").text =
        text != nullptr ? text : "";
}

void CStringArraySetAtString(MfcCStringArrayCompat& array, int index,
    const MfcCStringCompat& value) {
    array_checked_ref(array, index, "CStringArraySetAtString") = value;
}

MfcCStringCompat& CStringArrayElementAt(MfcCStringArrayCompat& array,
    int index) {
    return array_checked_ref(array, index, "CStringArrayElementAt");
}

MfcCStringCompat* CStringArrayGetData(MfcCStringArrayCompat& array) {
    return array_data(array);
}

const MfcCStringCompat* CStringArrayGetDataConst(
    const MfcCStringArrayCompat& array) {
    return array_data(array);
}

MfcCStringCompat CStringArraySubscript(const MfcCStringArrayCompat& array,
    int index) {
    return CStringArrayGetAt(array, index);
}

MfcCStringCompat& CStringArraySubscriptRef(MfcCStringArrayCompat& array,
    int index) {
    return CStringArrayElementAt(array, index);
}

void CStringArrayDestroyElements(MfcCStringCompat* values, int count) {
    validate_nonnegative(count, "count");
    for (int i = 0; i < count; ++i) {
        values[i].text.clear();
    }
}

void CStringArrayDestroyElement(MfcCStringCompat& value) {
    value.text.clear();
}

MfcCStringArrayCompat& CStringArraySetSize(MfcCStringArrayCompat& array,
    int new_size, int grow_by) {
    validate_nonnegative(new_size, "new_size");
    array.grow_by = grow_by;
    array.values.resize(static_cast<std::size_t>(new_size));
    return array;
}

void CStringArrayConstructElements(MfcCStringCompat* values, int count) {
    validate_nonnegative(count, "count");
    for (int i = 0; i < count; ++i) {
        values[i] = MfcCStringCompat{};
    }
}

MfcCStringCompat& CStringArrayConstructElement(MfcCStringCompat& value) {
    value.text.clear();
    return value;
}

int CStringArrayAppend(MfcCStringArrayCompat& destination,
    const MfcCStringArrayCompat& source) {
    const int old_size = static_cast<int>(destination.values.size());
    destination.values.insert(destination.values.end(), source.values.begin(),
        source.values.end());
    return old_size;
}

void CStringArrayCopyElements(MfcCStringCompat* destination,
    const MfcCStringCompat* source, int count) {
    validate_nonnegative(count, "count");
    for (int i = 0; i < count; ++i) {
        destination[i] = source[i];
    }
}

MfcCStringArrayCompat& CStringArrayCopy(MfcCStringArrayCompat& destination,
    const MfcCStringArrayCompat& source) {
    if (&destination != &source) {
        destination.values = source.values;
        destination.grow_by = source.grow_by;
    }
    return destination;
}

MfcCStringArrayCompat& CStringArrayFreeExtra(MfcCStringArrayCompat& array) {
    return array_free_extra(array);
}

MfcCStringArrayCompat& CStringArraySetAtGrowText(
    MfcCStringArrayCompat& array, int index, const char* text) {
    validate_nonnegative(index, "index");
    const std::size_t offset = static_cast<std::size_t>(index);
    if (offset >= array.values.size()) {
        array.values.resize(offset + 1);
    }
    array.values[offset].text = text != nullptr ? text : "";
    return array;
}

MfcCStringArrayCompat& CStringArraySetAtGrowString(
    MfcCStringArrayCompat& array, int index, const MfcCStringCompat& value) {
    validate_nonnegative(index, "index");
    const std::size_t offset = static_cast<std::size_t>(index);
    if (offset >= array.values.size()) {
        array.values.resize(offset + 1);
    }
    array.values[offset] = value;
    return array;
}

MfcCStringArrayCompat& CStringArrayInsertEmptyAt(
    MfcCStringArrayCompat& array, int index, int count) {
    validate_nonnegative(index, "index");
    validate_nonnegative(count, "count");
    const std::size_t offset = std::min<std::size_t>(
        static_cast<std::size_t>(index), array.values.size());
    array.values.insert(array.values.begin() + offset,
        static_cast<std::size_t>(count), MfcCStringCompat{});
    return array;
}

MfcCStringArrayCompat& CStringArrayInsertTextAt(MfcCStringArrayCompat& array,
    int index, const char* text, int count) {
    CStringArrayInsertEmptyAt(array, index, count);
    const std::size_t offset = static_cast<std::size_t>(index);
    for (int i = 0; i < count; ++i) {
        array.values[offset + static_cast<std::size_t>(i)].text =
            text != nullptr ? text : "";
    }
    return array;
}

MfcCStringArrayCompat& CStringArrayInsertStringAt(
    MfcCStringArrayCompat& array, int index, const MfcCStringCompat& value,
    int count) {
    CStringArrayInsertEmptyAt(array, index, count);
    const std::size_t offset = static_cast<std::size_t>(index);
    for (int i = 0; i < count; ++i) {
        array.values[offset + static_cast<std::size_t>(i)] = value;
    }
    return array;
}

MfcCStringArrayCompat& CStringArrayRemoveAt(MfcCStringArrayCompat& array,
    int index, int count) {
    return array_remove_at(array, index, count);
}

MfcCStringArrayCompat& CStringArrayInsertArrayAt(
    MfcCStringArrayCompat& array, int index,
    const MfcCStringArrayCompat& source) {
    return array_insert_array_at(array, index, source);
}

void CStringArraySerialize(MfcCStringArrayCompat& array, void* archive) {
    (void)array;
    (void)archive;
}

void CStringArrayDump(const MfcCStringArrayCompat& array) {
    AfxTraceOutput("CStringArray count=%zu\n", array.values.size());
}

void CStringArrayAssertValid(const MfcCStringArrayCompat& array) {
    array_assert_valid(array);
}

MfcCStringArrayCompat* DeleteCStringArrayScalarDtor(
    MfcCStringArrayCompat* array, unsigned flags) {
    return array_scalar_dtor(array, flags);
}

MfcCStringCompat* DeleteCStringScalarDtor(MfcCStringCompat* value,
    unsigned flags) {
    if (value == nullptr) {
        return nullptr;
    }
    value->text.clear();
    if ((flags & 1U) != 0U) {
        delete value;
    }
    return value;
}

MfcPlexCompat* PlexCreate(MfcPlexCompat*& head, int max_elements,
    int element_size) {
    if (max_elements <= 0 || element_size <= 0) {
        throw std::out_of_range("invalid CPlex allocation");
    }
    auto* plex = new MfcPlexCompat;
    plex->next = head;
    plex->data.resize(static_cast<std::size_t>(max_elements) *
        static_cast<std::size_t>(element_size));
    head = plex;
    return plex;
}

void PlexFreeDataChain(MfcPlexCompat* head) {
    while (head != nullptr) {
        MfcPlexCompat* next = head->next;
        delete head;
        head = next;
    }
}

unsigned MapPtrToPtrHashKey(void* key) {
    return static_cast<unsigned>(
        reinterpret_cast<std::uintptr_t>(key) >> 4);
}

MfcMapPtrToPtrCompat& ConstructMapPtrToPtr(MfcMapPtrToPtrCompat& map,
    int block_size) {
    map.entries.clear();
    map.hash_table_size = 17;
    map.block_size = block_size > 0 ? block_size : 10;
    return map;
}

MfcMapPtrToPtrCompat& MapPtrToPtrInitHashTable(MfcMapPtrToPtrCompat& map,
    int hash_size, bool) {
    if (hash_size <= 0) {
        throw std::out_of_range("hash_size");
    }
    map.hash_table_size = hash_size;
    map.entries.rehash(static_cast<std::size_t>(hash_size));
    return map;
}

void MapPtrToPtrRemoveAll(MfcMapPtrToPtrCompat& map) {
    map.entries.clear();
}

void DestructMapPtrToPtr(MfcMapPtrToPtrCompat& map) {
    MapPtrToPtrRemoveAll(map);
}

MfcMapPtrToPtrAssocCompat* MapPtrToPtrNewAssoc(MfcMapPtrToPtrCompat& map) {
    auto* assoc = new MfcMapPtrToPtrAssocCompat;
    assoc->key = nullptr;
    assoc->value = nullptr;
    (void)map;
    return assoc;
}

void MapPtrToPtrFreeAssoc(MfcMapPtrToPtrCompat& map,
    MfcMapPtrToPtrAssocCompat* assoc) {
    if (assoc == nullptr) {
        return;
    }
    if (assoc->key != nullptr) {
        map.entries.erase(assoc->key);
    } else {
        delete assoc;
    }
}

MfcMapPtrToPtrAssocCompat* MapPtrToPtrGetAssocAt(
    MfcMapPtrToPtrCompat& map, void* key, unsigned* hash) {
    if (hash != nullptr) {
        *hash = static_cast<unsigned>(
            map.hash_table_size > 0 ? MapPtrToPtrHashKey(key) %
            static_cast<unsigned>(map.hash_table_size) : 0);
    }
    auto found = map.entries.find(key);
    return found != map.entries.end() ? &found->second : nullptr;
}

void* MapPtrToPtrGetValueAt(MfcMapPtrToPtrCompat& map, void* key) {
    auto found = map.entries.find(key);
    return found != map.entries.end() ? found->second.value : nullptr;
}

bool MapPtrToPtrLookup(MfcMapPtrToPtrCompat& map, void* key, void*& value) {
    auto found = map.entries.find(key);
    if (found == map.entries.end()) {
        value = nullptr;
        return false;
    }
    value = found->second.value;
    return true;
}

void** MapPtrToPtrGetOrCreateValue(MfcMapPtrToPtrCompat& map, void* key) {
    MfcMapPtrToPtrAssocCompat& assoc = map.entries[key];
    assoc.key = key;
    return &assoc.value;
}

bool MapPtrToPtrRemoveKey(MfcMapPtrToPtrCompat& map, void* key) {
    return map.entries.erase(key) != 0;
}

MfcMapPtrToPtrAssocCompat* MapPtrToPtrGetStartPosition(
    MfcMapPtrToPtrCompat& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return &map.entries.begin()->second;
}

void MapPtrToPtrGetNextAssoc(MfcMapPtrToPtrCompat& map,
    MfcMapPtrToPtrAssocCompat*& position, void*& key, void*& value) {
    if (position == nullptr) {
        key = nullptr;
        value = nullptr;
        return;
    }
    MfcMapPtrToPtrAssocCompat* current = position;
    key = position->key;
    value = position->value;
    bool take_next = false;
    position = nullptr;
    for (auto& entry : map.entries) {
        if (take_next) {
            position = &entry.second;
            break;
        }
        if (&entry.second == current || entry.second.key == key) {
            take_next = true;
        }
    }
}

void MapPtrToPtrDump(const MfcMapPtrToPtrCompat& map) {
    AfxTraceOutput("CMapPtrToPtr count=%zu\n", map.entries.size());
}

void MapPtrToPtrAssertValid(const MfcMapPtrToPtrCompat& map) {
    if (map.hash_table_size <= 0 || map.block_size <= 0) {
        throw std::logic_error("invalid CMapPtrToPtr settings");
    }
}

MfcMapPtrToPtrCompat* DeleteMapPtrToPtrScalarDtor(
    MfcMapPtrToPtrCompat* map, unsigned flags) {
    if (map == nullptr) {
        return nullptr;
    }
    DestructMapPtrToPtr(*map);
    if ((flags & 1U) != 0U) {
        delete map;
    }
    return map;
}

unsigned MapWordHashKey(unsigned short key) {
    return static_cast<unsigned>(key) >> 4;
}

MfcMapWordToPtrCompat& ConstructMapWordToPtr(MfcMapWordToPtrCompat& map,
    int block_size) {
    map.entries.clear();
    map.hash_table_size = 17;
    map.block_size = block_size > 0 ? block_size : 10;
    return map;
}

MfcMapWordToPtrCompat& MapWordToPtrInitHashTable(
    MfcMapWordToPtrCompat& map, int hash_size, bool) {
    if (hash_size <= 0) {
        throw std::out_of_range("hash_size");
    }
    map.hash_table_size = hash_size;
    map.entries.rehash(static_cast<std::size_t>(hash_size));
    return map;
}

void MapWordToPtrRemoveAll(MfcMapWordToPtrCompat& map) {
    map.entries.clear();
}

void DestructMapWordToPtr(MfcMapWordToPtrCompat& map) {
    MapWordToPtrRemoveAll(map);
}

MfcMapWordToPtrAssocCompat* MapWordToPtrNewAssoc(
    MfcMapWordToPtrCompat& map) {
    (void)map;
    return new MfcMapWordToPtrAssocCompat;
}

void MapWordToPtrFreeAssoc(MfcMapWordToPtrCompat& map,
    MfcMapWordToPtrAssocCompat* assoc) {
    if (assoc == nullptr) {
        return;
    }
    auto found = map.entries.find(assoc->key);
    if (found != map.entries.end() && &found->second == assoc) {
        map.entries.erase(found);
    } else {
        delete assoc;
    }
}

MfcMapWordToPtrAssocCompat* MapWordToPtrGetAssocAt(
    MfcMapWordToPtrCompat& map, unsigned short key, unsigned* hash) {
    if (hash != nullptr) {
        *hash = static_cast<unsigned>(
            map.hash_table_size > 0 ? MapWordHashKey(key) %
            static_cast<unsigned>(map.hash_table_size) : 0);
    }
    auto found = map.entries.find(key);
    return found != map.entries.end() ? &found->second : nullptr;
}

bool MapWordToPtrLookup(MfcMapWordToPtrCompat& map, unsigned short key,
    void*& value) {
    auto found = map.entries.find(key);
    if (found == map.entries.end()) {
        value = nullptr;
        return false;
    }
    value = found->second.value;
    return true;
}

void** MapWordToPtrGetOrCreateValue(MfcMapWordToPtrCompat& map,
    unsigned short key) {
    MfcMapWordToPtrAssocCompat& assoc = map.entries[key];
    assoc.key = key;
    return &assoc.value;
}

bool MapWordToPtrRemoveKey(MfcMapWordToPtrCompat& map, unsigned short key) {
    return map.entries.erase(key) != 0;
}

MfcMapWordToPtrAssocCompat* MapWordToPtrGetStartPosition(
    MfcMapWordToPtrCompat& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return &map.entries.begin()->second;
}

void MapWordToPtrGetNextAssoc(MfcMapWordToPtrCompat& map,
    MfcMapWordToPtrAssocCompat*& position, unsigned short& key,
    void*& value) {
    if (position == nullptr) {
        key = 0;
        value = nullptr;
        return;
    }
    MfcMapWordToPtrAssocCompat* current = position;
    key = position->key;
    value = position->value;
    bool take_next = false;
    position = nullptr;
    for (auto& entry : map.entries) {
        if (take_next) {
            position = &entry.second;
            break;
        }
        if (&entry.second == current || entry.second.key == key) {
            take_next = true;
        }
    }
}

void MapWordToPtrDump(const MfcMapWordToPtrCompat& map) {
    AfxTraceOutput("CMapWordToPtr count=%zu\n", map.entries.size());
}

void MapWordToPtrAssertValid(const MfcMapWordToPtrCompat& map) {
    if (map.hash_table_size <= 0 || map.block_size <= 0) {
        throw std::logic_error("invalid CMapWordToPtr settings");
    }
}

MfcMapWordToPtrCompat* DeleteMapWordToPtrScalarDtor(
    MfcMapWordToPtrCompat* map, unsigned flags) {
    if (map == nullptr) {
        return nullptr;
    }
    DestructMapWordToPtr(*map);
    if ((flags & 1U) != 0U) {
        delete map;
    }
    return map;
}

unsigned MapPtrToWordHashKey(void* key) {
    return MapPtrToPtrHashKey(key);
}

MfcMapPtrToWordCompat& ConstructMapPtrToWord(MfcMapPtrToWordCompat& map,
    int block_size) {
    map.entries.clear();
    map.hash_table_size = 17;
    map.block_size = block_size > 0 ? block_size : 10;
    return map;
}

MfcMapPtrToWordCompat& MapPtrToWordInitHashTable(
    MfcMapPtrToWordCompat& map, int hash_size, bool) {
    if (hash_size <= 0) {
        throw std::out_of_range("hash_size");
    }
    map.hash_table_size = hash_size;
    map.entries.rehash(static_cast<std::size_t>(hash_size));
    return map;
}

void MapPtrToWordRemoveAll(MfcMapPtrToWordCompat& map) {
    map.entries.clear();
}

void DestructMapPtrToWord(MfcMapPtrToWordCompat& map) {
    MapPtrToWordRemoveAll(map);
}

MfcMapPtrToWordAssocCompat* MapPtrToWordNewAssoc(
    MfcMapPtrToWordCompat& map) {
    (void)map;
    return new MfcMapPtrToWordAssocCompat;
}

void MapPtrToWordFreeAssoc(MfcMapPtrToWordCompat& map,
    MfcMapPtrToWordAssocCompat* assoc) {
    if (assoc == nullptr) {
        return;
    }
    auto found = map.entries.find(assoc->key);
    if (found != map.entries.end() && &found->second == assoc) {
        map.entries.erase(found);
    } else {
        delete assoc;
    }
}

MfcMapPtrToWordAssocCompat* MapPtrToWordGetAssocAt(
    MfcMapPtrToWordCompat& map, void* key, unsigned* hash) {
    if (hash != nullptr) {
        *hash = static_cast<unsigned>(
            map.hash_table_size > 0 ? MapPtrToWordHashKey(key) %
            static_cast<unsigned>(map.hash_table_size) : 0);
    }
    auto found = map.entries.find(key);
    return found != map.entries.end() ? &found->second : nullptr;
}

bool MapPtrToWordLookup(MfcMapPtrToWordCompat& map, void* key,
    unsigned short& value) {
    auto found = map.entries.find(key);
    if (found == map.entries.end()) {
        value = 0;
        return false;
    }
    value = found->second.value;
    return true;
}

unsigned short* MapPtrToWordGetOrCreateValue(MfcMapPtrToWordCompat& map,
    void* key) {
    MfcMapPtrToWordAssocCompat& assoc = map.entries[key];
    assoc.key = key;
    return &assoc.value;
}

bool MapPtrToWordRemoveKey(MfcMapPtrToWordCompat& map, void* key) {
    return map.entries.erase(key) != 0;
}

MfcMapPtrToWordAssocCompat* MapPtrToWordGetStartPosition(
    MfcMapPtrToWordCompat& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return &map.entries.begin()->second;
}

void MapPtrToWordGetNextAssoc(MfcMapPtrToWordCompat& map,
    MfcMapPtrToWordAssocCompat*& position, void*& key,
    unsigned short& value) {
    if (position == nullptr) {
        key = nullptr;
        value = 0;
        return;
    }
    MfcMapPtrToWordAssocCompat* current = position;
    key = position->key;
    value = position->value;
    bool take_next = false;
    position = nullptr;
    for (auto& entry : map.entries) {
        if (take_next) {
            position = &entry.second;
            break;
        }
        if (&entry.second == current || entry.second.key == key) {
            take_next = true;
        }
    }
}

void MapPtrToWordDump(const MfcMapPtrToWordCompat& map) {
    AfxTraceOutput("CMapPtrToWord count=%zu\n", map.entries.size());
}

void MapPtrToWordAssertValid(const MfcMapPtrToWordCompat& map) {
    if (map.hash_table_size <= 0 || map.block_size <= 0) {
        throw std::logic_error("invalid CMapPtrToWord settings");
    }
}

MfcMapPtrToWordCompat* DeleteMapPtrToWordScalarDtor(
    MfcMapPtrToWordCompat* map, unsigned flags) {
    if (map == nullptr) {
        return nullptr;
    }
    DestructMapPtrToWord(*map);
    if ((flags & 1U) != 0U) {
        delete map;
    }
    return map;
}

MfcMapWordToObCompat& ConstructMapWordToOb(MfcMapWordToObCompat& map,
    int block_size) {
    map.entries.clear();
    map.hash_table_size = 17;
    map.block_size = block_size > 0 ? block_size : 10;
    return map;
}

MfcMapWordToObCompat& MapWordToObInitHashTable(
    MfcMapWordToObCompat& map, int hash_size, bool) {
    if (hash_size <= 0) {
        throw std::out_of_range("hash_size");
    }
    map.hash_table_size = hash_size;
    map.entries.rehash(static_cast<std::size_t>(hash_size));
    return map;
}

void MapWordToObRemoveAll(MfcMapWordToObCompat& map) {
    map.entries.clear();
}

void DestructMapWordToOb(MfcMapWordToObCompat& map) {
    MapWordToObRemoveAll(map);
}

MfcMapWordToObAssocCompat* MapWordToObNewAssoc(
    MfcMapWordToObCompat& map) {
    (void)map;
    return new MfcMapWordToObAssocCompat;
}

void MapWordToObFreeAssoc(MfcMapWordToObCompat& map,
    MfcMapWordToObAssocCompat* assoc) {
    if (assoc == nullptr) {
        return;
    }
    auto found = map.entries.find(assoc->key);
    if (found != map.entries.end() && &found->second == assoc) {
        map.entries.erase(found);
    } else {
        delete assoc;
    }
}

MfcMapWordToObAssocCompat* MapWordToObGetAssocAt(
    MfcMapWordToObCompat& map, unsigned short key, unsigned* hash) {
    if (hash != nullptr) {
        *hash = static_cast<unsigned>(
            map.hash_table_size > 0 ? MapWordHashKey(key) %
            static_cast<unsigned>(map.hash_table_size) : 0);
    }
    auto found = map.entries.find(key);
    return found != map.entries.end() ? &found->second : nullptr;
}

bool MapWordToObLookup(MfcMapWordToObCompat& map, unsigned short key,
    void*& value) {
    auto found = map.entries.find(key);
    if (found == map.entries.end()) {
        value = nullptr;
        return false;
    }
    value = found->second.value;
    return true;
}

void** MapWordToObGetOrCreateValue(MfcMapWordToObCompat& map,
    unsigned short key) {
    MfcMapWordToObAssocCompat& assoc = map.entries[key];
    assoc.key = key;
    return &assoc.value;
}

bool MapWordToObRemoveKey(MfcMapWordToObCompat& map, unsigned short key) {
    return map.entries.erase(key) != 0;
}

MfcMapWordToObAssocCompat* MapWordToObGetStartPosition(
    MfcMapWordToObCompat& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return &map.entries.begin()->second;
}

void MapWordToObGetNextAssoc(MfcMapWordToObCompat& map,
    MfcMapWordToObAssocCompat*& position, unsigned short& key,
    void*& value) {
    if (position == nullptr) {
        key = 0;
        value = nullptr;
        return;
    }
    MfcMapWordToObAssocCompat* current = position;
    key = position->key;
    value = position->value;
    bool take_next = false;
    position = nullptr;
    for (auto& entry : map.entries) {
        if (take_next) {
            position = &entry.second;
            break;
        }
        if (&entry.second == current || entry.second.key == key) {
            take_next = true;
        }
    }
}

void MapWordToObSerialize(MfcMapWordToObCompat& map, void* archive) {
    (void)map;
    (void)archive;
}

void MapWordToObDump(const MfcMapWordToObCompat& map) {
    AfxTraceOutput("CMapWordToOb count=%zu\n", map.entries.size());
}

void MapWordToObAssertValid(const MfcMapWordToObCompat& map) {
    if (map.hash_table_size <= 0 || map.block_size <= 0) {
        throw std::logic_error("invalid CMapWordToOb settings");
    }
}

MfcMapWordToObCompat* DeleteMapWordToObScalarDtor(
    MfcMapWordToObCompat* map, unsigned flags) {
    if (map == nullptr) {
        return nullptr;
    }
    DestructMapWordToOb(*map);
    if ((flags & 1U) != 0U) {
        delete map;
    }
    return map;
}

unsigned MapWordToObHashKey(unsigned short key) {
    return MapWordHashKey(key);
}

unsigned MapStringHashKey(const char* key) {
    unsigned hash = 0;
    if (key == nullptr) {
        return hash;
    }
    while (*key != '\0') {
        hash = hash * 0x21u + static_cast<unsigned char>(*key++);
    }
    return hash;
}

void MapStringDestructElement(MfcCStringCompat& key) {
    key.text.clear();
}

MfcMapStringToPtrCompat& ConstructMapStringToPtr(
    MfcMapStringToPtrCompat& map, int block_size) {
    map.entries.clear();
    map.hash_table_size = 17;
    map.block_size = block_size > 0 ? block_size : 10;
    return map;
}

MfcMapStringToPtrCompat& MapStringToPtrInitHashTable(
    MfcMapStringToPtrCompat& map, int hash_size, bool) {
    if (hash_size <= 0) {
        throw std::out_of_range("hash_size");
    }
    map.hash_table_size = hash_size;
    map.entries.rehash(static_cast<std::size_t>(hash_size));
    return map;
}

void MapStringToPtrRemoveAll(MfcMapStringToPtrCompat& map) {
    map.entries.clear();
}

void DestructMapStringToPtr(MfcMapStringToPtrCompat& map) {
    MapStringToPtrRemoveAll(map);
}

MfcMapStringToPtrAssocCompat* MapStringToPtrNewAssoc(
    MfcMapStringToPtrCompat& map) {
    (void)map;
    return new MfcMapStringToPtrAssocCompat;
}

void MapStringToPtrFreeAssoc(MfcMapStringToPtrCompat& map,
    MfcMapStringToPtrAssocCompat* assoc) {
    if (assoc == nullptr) {
        return;
    }
    auto found = map.entries.find(assoc->key);
    if (found != map.entries.end() && &found->second == assoc) {
        map.entries.erase(found);
    } else {
        delete assoc;
    }
}

MfcMapStringToPtrAssocCompat* MapStringToPtrGetAssocAt(
    MfcMapStringToPtrCompat& map, const char* key, unsigned* hash) {
    const std::string lookup_key = key != nullptr ? key : "";
    if (hash != nullptr) {
        *hash = static_cast<unsigned>(
            map.hash_table_size > 0 ? MapStringHashKey(lookup_key.c_str()) %
            static_cast<unsigned>(map.hash_table_size) : 0);
    }
    auto found = map.entries.find(lookup_key);
    return found != map.entries.end() ? &found->second : nullptr;
}

bool MapStringToPtrLookup(MfcMapStringToPtrCompat& map, const char* key,
    void*& value) {
    auto found = map.entries.find(key != nullptr ? key : "");
    if (found == map.entries.end()) {
        value = nullptr;
        return false;
    }
    value = found->second.value;
    return true;
}

bool MapStringToPtrLookupKey(MfcMapStringToPtrCompat& map, const char* key,
    std::string& actual_key) {
    auto found = map.entries.find(key != nullptr ? key : "");
    if (found == map.entries.end()) {
        actual_key.clear();
        return false;
    }
    actual_key = found->second.key;
    return true;
}

void** MapStringToPtrGetOrCreateValue(MfcMapStringToPtrCompat& map,
    const char* key) {
    const std::string lookup_key = key != nullptr ? key : "";
    MfcMapStringToPtrAssocCompat& assoc = map.entries[lookup_key];
    assoc.key = lookup_key;
    return &assoc.value;
}

bool MapStringToPtrRemoveKey(MfcMapStringToPtrCompat& map, const char* key) {
    return map.entries.erase(key != nullptr ? key : "") != 0;
}

MfcMapStringToPtrAssocCompat* MapStringToPtrGetStartPosition(
    MfcMapStringToPtrCompat& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return &map.entries.begin()->second;
}

void MapStringToPtrGetNextAssoc(MfcMapStringToPtrCompat& map,
    MfcMapStringToPtrAssocCompat*& position, std::string& key, void*& value) {
    if (position == nullptr) {
        key.clear();
        value = nullptr;
        return;
    }
    MfcMapStringToPtrAssocCompat* current = position;
    key = position->key;
    value = position->value;
    bool take_next = false;
    position = nullptr;
    for (auto& entry : map.entries) {
        if (take_next) {
            position = &entry.second;
            break;
        }
        if (&entry.second == current || entry.second.key == key) {
            take_next = true;
        }
    }
}

void MapStringToPtrDump(const MfcMapStringToPtrCompat& map) {
    AfxTraceOutput("CMapStringToPtr count=%zu\n", map.entries.size());
}

void MapStringToPtrAssertValid(const MfcMapStringToPtrCompat& map) {
    if (map.hash_table_size <= 0 || map.block_size <= 0) {
        throw std::logic_error("invalid CMapStringToPtr settings");
    }
}

MfcMapStringToPtrCompat* DeleteMapStringToPtrScalarDtor(
    MfcMapStringToPtrCompat* map, unsigned flags) {
    if (map == nullptr) {
        return nullptr;
    }
    DestructMapStringToPtr(*map);
    if ((flags & 1U) != 0U) {
        delete map;
    }
    return map;
}

MfcMapStringToObCompat& ConstructMapStringToOb(
    MfcMapStringToObCompat& map, int block_size) {
    map.entries.clear();
    map.hash_table_size = 17;
    map.block_size = block_size > 0 ? block_size : 10;
    return map;
}

MfcMapStringToObCompat& MapStringToObInitHashTable(
    MfcMapStringToObCompat& map, int hash_size, bool) {
    if (hash_size <= 0) {
        throw std::out_of_range("hash_size");
    }
    map.hash_table_size = hash_size;
    map.entries.rehash(static_cast<std::size_t>(hash_size));
    return map;
}

void MapStringToObRemoveAll(MfcMapStringToObCompat& map) {
    map.entries.clear();
}

void DestructMapStringToOb(MfcMapStringToObCompat& map) {
    MapStringToObRemoveAll(map);
}

MfcMapStringToObAssocCompat* MapStringToObNewAssoc(
    MfcMapStringToObCompat& map) {
    (void)map;
    return new MfcMapStringToObAssocCompat;
}

void MapStringToObFreeAssoc(MfcMapStringToObCompat& map,
    MfcMapStringToObAssocCompat* assoc) {
    if (assoc == nullptr) {
        return;
    }
    auto found = map.entries.find(assoc->key);
    if (found != map.entries.end() && &found->second == assoc) {
        map.entries.erase(found);
    } else {
        delete assoc;
    }
}

MfcMapStringToObAssocCompat* MapStringToObGetAssocAt(
    MfcMapStringToObCompat& map, const char* key, unsigned* hash) {
    const std::string lookup_key = key != nullptr ? key : "";
    if (hash != nullptr) {
        *hash = static_cast<unsigned>(
            map.hash_table_size > 0 ? MapStringHashKey(lookup_key.c_str()) %
            static_cast<unsigned>(map.hash_table_size) : 0);
    }
    auto found = map.entries.find(lookup_key);
    return found != map.entries.end() ? &found->second : nullptr;
}

bool MapStringToObLookup(MfcMapStringToObCompat& map, const char* key,
    void*& value) {
    auto found = map.entries.find(key != nullptr ? key : "");
    if (found == map.entries.end()) {
        value = nullptr;
        return false;
    }
    value = found->second.value;
    return true;
}

bool MapStringToObLookupKey(MfcMapStringToObCompat& map, const char* key,
    std::string& actual_key) {
    auto found = map.entries.find(key != nullptr ? key : "");
    if (found == map.entries.end()) {
        actual_key.clear();
        return false;
    }
    actual_key = found->second.key;
    return true;
}

void** MapStringToObGetOrCreateValue(MfcMapStringToObCompat& map,
    const char* key) {
    const std::string lookup_key = key != nullptr ? key : "";
    MfcMapStringToObAssocCompat& assoc = map.entries[lookup_key];
    assoc.key = lookup_key;
    return &assoc.value;
}

bool MapStringToObRemoveKey(MfcMapStringToObCompat& map, const char* key) {
    return map.entries.erase(key != nullptr ? key : "") != 0;
}

MfcMapStringToObAssocCompat* MapStringToObGetStartPosition(
    MfcMapStringToObCompat& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return &map.entries.begin()->second;
}

void MapStringToObGetNextAssoc(MfcMapStringToObCompat& map,
    MfcMapStringToObAssocCompat*& position, std::string& key, void*& value) {
    if (position == nullptr) {
        key.clear();
        value = nullptr;
        return;
    }
    MfcMapStringToObAssocCompat* current = position;
    key = position->key;
    value = position->value;
    bool take_next = false;
    position = nullptr;
    for (auto& entry : map.entries) {
        if (take_next) {
            position = &entry.second;
            break;
        }
        if (&entry.second == current || entry.second.key == key) {
            take_next = true;
        }
    }
}

void MapStringToObSerialize(MfcMapStringToObCompat& map, void* archive) {
    (void)map;
    (void)archive;
}

void MapStringToObDump(const MfcMapStringToObCompat& map) {
    AfxTraceOutput("CMapStringToOb count=%zu\n", map.entries.size());
}

void MapStringToObAssertValid(const MfcMapStringToObCompat& map) {
    if (map.hash_table_size <= 0 || map.block_size <= 0) {
        throw std::logic_error("invalid CMapStringToOb settings");
    }
}

MfcMapStringToObCompat* DeleteMapStringToObScalarDtor(
    MfcMapStringToObCompat* map, unsigned flags) {
    if (map == nullptr) {
        return nullptr;
    }
    DestructMapStringToOb(*map);
    if ((flags & 1U) != 0U) {
        delete map;
    }
    return map;
}

unsigned MapStringToObHashKey(const char* key) {
    return MapStringHashKey(key);
}

void MapStringToObDestructElement(MfcCStringCompat& key) {
    MapStringDestructElement(key);
}

MfcMapStringToStringCompat& ConstructMapStringToString(
    MfcMapStringToStringCompat& map, int block_size) {
    map.entries.clear();
    map.hash_table_size = 17;
    map.block_size = block_size > 0 ? block_size : 10;
    return map;
}

MfcMapStringToStringCompat& MapStringToStringInitHashTable(
    MfcMapStringToStringCompat& map, int hash_size, bool) {
    if (hash_size <= 0) {
        throw std::out_of_range("hash_size");
    }
    map.hash_table_size = hash_size;
    map.entries.rehash(static_cast<std::size_t>(hash_size));
    return map;
}

void MapStringToStringRemoveAll(MfcMapStringToStringCompat& map) {
    map.entries.clear();
}

void MapStringToStringDestructElement(MfcCStringCompat& value) {
    value.text.clear();
}

void DestructMapStringToString(MfcMapStringToStringCompat& map) {
    MapStringToStringRemoveAll(map);
}

MfcMapStringToStringAssocCompat* MapStringToStringNewAssoc(
    MfcMapStringToStringCompat& map) {
    (void)map;
    return new MfcMapStringToStringAssocCompat;
}

void MapStringToStringConstructValueElement(MfcCStringCompat& value) {
    value.text.clear();
}

void MapStringToStringFreeAssoc(MfcMapStringToStringCompat& map,
    MfcMapStringToStringAssocCompat* assoc) {
    if (assoc == nullptr) {
        return;
    }
    auto found = map.entries.find(assoc->key);
    if (found != map.entries.end() && &found->second == assoc) {
        map.entries.erase(found);
    } else {
        delete assoc;
    }
}

MfcMapStringToStringAssocCompat* MapStringToStringGetAssocAt(
    MfcMapStringToStringCompat& map, const char* key, unsigned* hash) {
    const std::string lookup_key = key != nullptr ? key : "";
    if (hash != nullptr) {
        *hash = static_cast<unsigned>(
            map.hash_table_size > 0 ? MapStringHashKey(lookup_key.c_str()) %
            static_cast<unsigned>(map.hash_table_size) : 0);
    }
    auto found = map.entries.find(lookup_key);
    return found != map.entries.end() ? &found->second : nullptr;
}

bool MapStringToStringLookup(MfcMapStringToStringCompat& map,
    const char* key, std::string& value) {
    auto found = map.entries.find(key != nullptr ? key : "");
    if (found == map.entries.end()) {
        value.clear();
        return false;
    }
    value = found->second.value;
    return true;
}

bool MapStringToStringLookupKey(MfcMapStringToStringCompat& map,
    const char* key, std::string& actual_key) {
    auto found = map.entries.find(key != nullptr ? key : "");
    if (found == map.entries.end()) {
        actual_key.clear();
        return false;
    }
    actual_key = found->second.key;
    return true;
}

std::string& MapStringToStringGetOrCreateValue(
    MfcMapStringToStringCompat& map, const char* key) {
    const std::string lookup_key = key != nullptr ? key : "";
    MfcMapStringToStringAssocCompat& assoc = map.entries[lookup_key];
    assoc.key = lookup_key;
    return assoc.value;
}

bool MapStringToStringRemoveKey(MfcMapStringToStringCompat& map,
    const char* key) {
    return map.entries.erase(key != nullptr ? key : "") != 0;
}

MfcMapStringToStringAssocCompat* MapStringToStringGetStartPosition(
    MfcMapStringToStringCompat& map) {
    if (map.entries.empty()) {
        return nullptr;
    }
    return &map.entries.begin()->second;
}

void MapStringToStringGetNextAssoc(MfcMapStringToStringCompat& map,
    MfcMapStringToStringAssocCompat*& position, std::string& key,
    std::string& value) {
    if (position == nullptr) {
        key.clear();
        value.clear();
        return;
    }
    MfcMapStringToStringAssocCompat* current = position;
    key = position->key;
    value = position->value;
    bool take_next = false;
    position = nullptr;
    for (auto& entry : map.entries) {
        if (take_next) {
            position = &entry.second;
            break;
        }
        if (&entry.second == current || entry.second.key == key) {
            take_next = true;
        }
    }
}

void MapStringToStringSerialize(MfcMapStringToStringCompat& map,
    void* archive) {
    (void)map;
    (void)archive;
}

void MapStringToStringDump(const MfcMapStringToStringCompat& map) {
    AfxTraceOutput("CMapStringToString count=%zu\n", map.entries.size());
}

void MapStringToStringAssertValid(const MfcMapStringToStringCompat& map) {
    if (map.hash_table_size <= 0 || map.block_size <= 0) {
        throw std::logic_error("invalid CMapStringToString settings");
    }
}

MfcMapStringToStringCompat* DeleteMapStringToStringScalarDtor(
    MfcMapStringToStringCompat* map, unsigned flags) {
    if (map == nullptr) {
        return nullptr;
    }
    DestructMapStringToString(*map);
    if ((flags & 1U) != 0U) {
        delete map;
    }
    return map;
}

unsigned MapStringToStringHashKey(const char* key) {
    return MapStringHashKey(key);
}

int MapWordToPtrGetCount(const MfcMapWordToPtrCompat& map) {
    return map_get_count(map);
}

bool MapWordToPtrIsEmpty(const MfcMapWordToPtrCompat& map) {
    return map_is_empty(map);
}

void MapWordToPtrSetAt(MfcMapWordToPtrCompat& map, unsigned short key,
    void* value) {
    *MapWordToPtrGetOrCreateValue(map, key) = value;
}

void* MapWordToPtrGetStartPositionSentinel(
    const MfcMapWordToPtrCompat& map) {
    return map_start_position_sentinel(map);
}

int MapWordToPtrGetHashTableSize(const MfcMapWordToPtrCompat& map) {
    return map_hash_table_size(map);
}

int MapPtrToWordGetCount(const MfcMapPtrToWordCompat& map) {
    return map_get_count(map);
}

bool MapPtrToWordIsEmpty(const MfcMapPtrToWordCompat& map) {
    return map_is_empty(map);
}

void MapPtrToWordSetAt(MfcMapPtrToWordCompat& map, void* key,
    unsigned short value) {
    *MapPtrToWordGetOrCreateValue(map, key) = value;
}

void* MapPtrToWordGetStartPositionSentinel(
    const MfcMapPtrToWordCompat& map) {
    return map_start_position_sentinel(map);
}

int MapPtrToWordGetHashTableSize(const MfcMapPtrToWordCompat& map) {
    return map_hash_table_size(map);
}

int MapPtrToPtrGetCount(const MfcMapPtrToPtrCompat& map) {
    return map_get_count(map);
}

bool MapPtrToPtrIsEmpty(const MfcMapPtrToPtrCompat& map) {
    return map_is_empty(map);
}

void MapPtrToPtrSetAt(MfcMapPtrToPtrCompat& map, void* key, void* value) {
    *MapPtrToPtrGetOrCreateValue(map, key) = value;
}

void* MapPtrToPtrGetStartPositionSentinel(
    const MfcMapPtrToPtrCompat& map) {
    return map_start_position_sentinel(map);
}

int MapPtrToPtrGetHashTableSize(const MfcMapPtrToPtrCompat& map) {
    return map_hash_table_size(map);
}

int MapWordToObGetCount(const MfcMapWordToObCompat& map) {
    return map_get_count(map);
}

bool MapWordToObIsEmpty(const MfcMapWordToObCompat& map) {
    return map_is_empty(map);
}

void MapWordToObSetAt(MfcMapWordToObCompat& map, unsigned short key,
    void* value) {
    *MapWordToObGetOrCreateValue(map, key) = value;
}

void* MapWordToObGetStartPositionSentinel(
    const MfcMapWordToObCompat& map) {
    return map_start_position_sentinel(map);
}

int MapWordToObGetHashTableSize(const MfcMapWordToObCompat& map) {
    return map_hash_table_size(map);
}

int MapStringToPtrGetCount(const MfcMapStringToPtrCompat& map) {
    return map_get_count(map);
}

bool MapStringToPtrIsEmpty(const MfcMapStringToPtrCompat& map) {
    return map_is_empty(map);
}

void MapStringToPtrSetAt(MfcMapStringToPtrCompat& map, const char* key,
    void* value) {
    *MapStringToPtrGetOrCreateValue(map, key) = value;
}

void* MapStringToPtrGetStartPositionSentinel(
    const MfcMapStringToPtrCompat& map) {
    return map_start_position_sentinel(map);
}

int MapStringToPtrGetHashTableSize(const MfcMapStringToPtrCompat& map) {
    return map_hash_table_size(map);
}

int MapStringToObGetCount(const MfcMapStringToObCompat& map) {
    return map_get_count(map);
}

bool MapStringToObIsEmpty(const MfcMapStringToObCompat& map) {
    return map_is_empty(map);
}

void MapStringToObSetAt(MfcMapStringToObCompat& map, const char* key,
    void* value) {
    *MapStringToObGetOrCreateValue(map, key) = value;
}

void* MapStringToObGetStartPositionSentinel(
    const MfcMapStringToObCompat& map) {
    return map_start_position_sentinel(map);
}

int MapStringToObGetHashTableSize(const MfcMapStringToObCompat& map) {
    return map_hash_table_size(map);
}

int MapStringToStringGetCount(const MfcMapStringToStringCompat& map) {
    return map_get_count(map);
}

bool MapStringToStringIsEmpty(const MfcMapStringToStringCompat& map) {
    return map_is_empty(map);
}

void MapStringToStringSetAtText(MfcMapStringToStringCompat& map,
    const char* key, const char* value) {
    MapStringToStringGetOrCreateValue(map, key) =
        value != nullptr ? value : "";
}

void* MapStringToStringGetStartPositionSentinel(
    const MfcMapStringToStringCompat& map) {
    return map_start_position_sentinel(map);
}

int MapStringToStringGetHashTableSize(
    const MfcMapStringToStringCompat& map) {
    return map_hash_table_size(map);
}

} // namespace ranker
