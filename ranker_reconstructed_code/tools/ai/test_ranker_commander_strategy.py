import json,tempfile,unittest
from pathlib import Path
from ranker_commander_strategy import parse_profile,validate_strategy,write_strategy

class StrategyProfileTest(unittest.TestCase):
    def test_rejects_ambiguous_or_invalid_native_parameters(self):
        self.assertEqual(parse_profile(b'JWELFSTRAT1\n0 0 0 0 0 0\n'),[0.]*6)
        for raw in (b'',b'JWELFSTRAT1 0 0 0 0 0',b'JWELFSTRAT1 0 0 0 0 0 0 extra',
                    b'JWELFSTRAT1 nan 0 0 0 0 0',b'JWELFSTRAT1 0 0 0 0 0 inf',
                    b'JWELFSTRAT1 .5 0 0 0 0 0',b'JWELFSTRAT1 0 .51 0 0 0 0',
                    b'JWELFSTRAT1 0 0 0 0 0 0\x00'):
            with self.subTest(raw=raw),self.assertRaises((ValueError,UnicodeError)):parse_profile(raw)

    def test_native_bytes_values_and_executor_must_match(self):
        root=Path(__file__).resolve().parents[3]
        with tempfile.TemporaryDirectory(dir=root/'debug_artifacts/commander') as temporary:
            directory=Path(temporary).resolve();self.assertTrue(directory.is_relative_to(root))
            exe=directory/'ranker_rebuild.exe';exe.write_bytes(b'test executor identity')
            binding=write_strategy(directory/'profile',[2,.25,-.5,3,-8,-.2],exe,purpose='search')
            d=binding['definition']
            line=f"[ElfStrategyProfile] crc={d['profile_crc32']:08x} values="+','.join(format(x,'.17g') for x in d['parameters'])+f" path={d['profile_path']}"
            validate_strategy(binding,executable_sha=d['executable_sha256'],log=line)
            for log in ('',line+'\n'+line,line.replace('values=2,','values=3,'),line.replace('profile.txt','wrong.txt')):
                with self.subTest(log=log),self.assertRaises(ValueError):validate_strategy(binding,log=log)
            with self.assertRaises(ValueError):validate_strategy(binding,executable_sha='wrong',log=line)
            Path(d['profile_path']).write_bytes(b'JWELFSTRAT1\n0 0 0 0 0 0\n')
            with self.assertRaises(ValueError):validate_strategy(binding,log=line)

if __name__=='__main__':unittest.main()
