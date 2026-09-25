import sys, json, unittest
sys.path.insert(0, 'tests'); sys.path.insert(0, 'backend')
import test_system_prompt as t
from relay_core import board_tools
class M(t.PromptFixture):
    def runTest(self):
        a = self.agent(board=False)
        p = a.system_prompt()
        print('PROMPT', len(p.encode()))
        secs = a.prompt_sections() if hasattr(a,'prompt_sections') else None
        if secs:
            for s in secs:
                s = s if isinstance(s,str) else str(s)
                print('  sec', len(s.encode()), repr(s[:70]))
        tools = a.tools()
        print('TOOLS', len(json.dumps(tools, ensure_ascii=False).encode()))
        for s in sorted(tools, key=lambda s: -len(json.dumps(s, ensure_ascii=False))):
            print('  tool', len(json.dumps(s, ensure_ascii=False).encode()), s['function']['name'])
        b = self.agent()
        print('BOARD PROMPT', len(b.system_prompt().encode()))
        print('POLICY', len(board_tools.prompt_section(b.board).encode()))
r = unittest.TextTestRunner().run(M())
