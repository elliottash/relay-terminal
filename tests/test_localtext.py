# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reasoning tags and text tool calls in a local model's content (backend/relay_core/localtext.py)."""
import json
import unittest

from relay_core.localtext import ThinkSplitter, recover_tool_calls, split_reasoning, strip_tool_fragments


def tool(name, **properties):
    return {'type': 'function', 'function': {'name': name, 'description': '', 'parameters': {
        'type': 'object', 'properties': properties, 'required': list(properties)}}}


TOOLS = [tool('run_command', command={'type': 'string'}, timeout={'type': 'integer'}),
         tool('read_file', path={'type': 'string'})]


def stream(chunks):
    splitter = ThinkSplitter()
    parts = [p for chunk in chunks for p in splitter.feed(chunk)] + splitter.flush()
    return (''.join(t for k, t in parts if k == 'content'), ''.join(t for k, t in parts if k == 'thinking'))


class ThinkSplitterTests(unittest.TestCase):
    def test_a_closed_block(self):
        self.assertEqual(stream(['<think>plan it</think>The answer.']), ('The answer.', 'plan it'))

    def test_every_split_point_gives_the_same_answer(self):
        text = 'Before <think>step one\nstep two</think> after <thinking>more</thinking> end'
        whole = stream([text])
        self.assertEqual(whole, ('Before  after  end', 'step one\nstep twomore'))
        for cut in range(1, len(text)):
            self.assertEqual(stream([text[:cut], text[cut:]]), whole, cut)
        self.assertEqual(stream(list(text)), whole)                 # one character at a time

    def test_an_opener_never_closed_is_all_reasoning(self):
        self.assertEqual(stream(['Sure. <think>I should first', ' look at the']), ('Sure. ', 'I should first look at the'))

    def test_a_less_than_sign_is_not_held_forever(self):
        self.assertEqual(stream(['if a <', ' b then']), ('if a < b then', ''))
        self.assertEqual(stream(['x <thi', 'ngy> y']), ('x <thingy> y', ''))
        self.assertEqual(stream(['ends with <th']), ('ends with <th', ''))

    def test_tags_are_matched_whatever_their_case_and_other_tags_are_text(self):
        self.assertEqual(stream(['<THINK>a</Think>b <b>bold</b>']), ('b <b>bold</b>', 'a'))

    def test_a_stray_closer_is_left_for_the_finished_text(self):
        self.assertEqual(stream(['reasoned here</think>Answer']), ('reasoned here</think>Answer', ''))


class SplitReasoningTests(unittest.TestCase):
    def test_plain_text_is_untouched(self):
        self.assertEqual(split_reasoning('Just an answer.\n'), ('Just an answer.\n', ''))
        self.assertEqual(split_reasoning(''), ('', ''))
        self.assertEqual(split_reasoning(None), ('', ''))

    def test_closed_unterminated_and_dangling(self):
        self.assertEqual(split_reasoning('<think>\nplan\n</think>\n\nDone.'), ('Done.', 'plan'))
        self.assertEqual(split_reasoning('<think>cut off mid'), ('', 'cut off mid'))
        self.assertEqual(split_reasoning('the template opened the tag\n</think>\n\nDone.'),
                         ('Done.', 'the template opened the tag'))


class RecoverToolCallsTests(unittest.TestCase):
    def one(self, content, tools=TOOLS):
        calls = recover_tool_calls(content, tools)
        self.assertIsNotNone(calls, content)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0]['type'], 'function')
        self.assertIsInstance(calls[0]['function']['arguments'], str)
        return calls[0]['function']['name'], json.loads(calls[0]['function']['arguments'])

    def test_the_formats_local_models_write(self):
        want = ('run_command', {'command': 'ls -la'})
        self.assertEqual(self.one('<tool_call>\n{"name": "run_command", "arguments": {"command": "ls -la"}}\n</tool_call>'), want)
        self.assertEqual(self.one('[TOOL_REQUEST]{"name": "run_command", "arguments": {"command": "ls -la"}}[END_TOOL_REQUEST]'), want)
        self.assertEqual(self.one('```json\n{"name": "run_command", "arguments": {"command": "ls -la"}}\n```'), want)
        self.assertEqual(self.one('  {"name": "run_command", "parameters": {"command": "ls -la"}}  '), want)
        self.assertEqual(self.one('<tool_call>{"name": "run_command", "arguments": "{\\"command\\": \\"ls -la\\"}"}</tool_call>'), want)

    def test_qwen_xml_parameters_take_their_declared_type(self):
        text = ('<tool_call>\n<function=run_command>\n<parameter=command>\necho "a\nb"\n</parameter>\n'
                '<parameter=timeout>\n30\n</parameter>\n</function>\n</tool_call>')
        self.assertEqual(self.one(text), ('run_command', {'command': 'echo "a\nb"', 'timeout': 30}))

    def test_atem_invoke_blocks_with_or_without_their_wrapper(self):
        inner = ('<atem:invoke name="run_command">\n<atem:parameter name="command">ls -la</atem:parameter>\n'
                 '<atem:parameter name="timeout">30</atem:parameter>\n</atem:invoke>')
        want = ('run_command', {'command': 'ls -la', 'timeout': 30})
        self.assertEqual(self.one(inner), want)
        self.assertEqual(self.one(f'<atem:function_calls>\n{inner}\n</atem:function_calls>'), want)
        multiline = ('<atem:function_calls><atem:invoke name="run_command"><atem:parameter name="command">\n'
                     'echo "a\nb"\n</atem:parameter></atem:invoke></atem:function_calls>')
        self.assertEqual(self.one(multiline), ('run_command', {'command': 'echo "a\nb"'}))

    def test_dsml_parameters_say_whether_they_are_text_or_json(self):
        text = ('<｜DSML｜calls><｜DSML｜invoke name="run_command">'
                '<｜DSML｜parameter name="command" string="true">ls -la</｜DSML｜parameter>'
                '<｜DSML｜parameter name="timeout" string="false">30</｜DSML｜parameter>'
                '</｜DSML｜invoke></｜DSML｜calls>')
        self.assertEqual(self.one(text), ('run_command', {'command': 'ls -la', 'timeout': 30}))
        # string="true" keeps text that happens to look like JSON as text.
        literal = ('<｜DSML｜invoke name="read_file">\n  <｜DSML｜parameter name="path" '
                   'string="true">123</｜DSML｜parameter>\n</｜DSML｜invoke>\n')
        self.assertEqual(self.one(literal), ('read_file', {'path': '123'}))

    def test_dsml_spellings_deepseek_versions_differ_on(self):
        # V4 writes <｜DSML｜tool_calls> and no space; V4.1 <｜DSML｜ calls> and a space in every tag.
        spaced = ('<｜DSML｜ calls>\n<｜DSML｜ invoke name="read_file">\n'
                  '<｜DSML｜ parameter name="path" string="true">a.txt</｜DSML｜ parameter>\n'
                  '</｜DSML｜ invoke>\n</｜DSML｜ calls>')
        self.assertEqual(self.one(spaced), ('read_file', {'path': 'a.txt'}))
        wrapped = ('<｜DSML｜tool_calls><｜DSML｜invoke name="read_file">'
                   '<｜DSML｜parameter name="path">a.txt</｜DSML｜parameter>'
                   '</｜DSML｜invoke></｜DSML｜tool_calls>')
        self.assertEqual(self.one(wrapped), ('read_file', {'path': 'a.txt'}))

    def test_several_blocks_keep_their_order_and_get_distinct_ids(self):
        text = ('<tool_call>{"name": "read_file", "arguments": {"path": "a"}}</tool_call>\n'
                '<tool_call>{"name": "read_file", "arguments": {"path": "b"}}</tool_call>')
        calls = recover_tool_calls(text, TOOLS)
        self.assertEqual([json.loads(c['function']['arguments'])['path'] for c in calls], ['a', 'b'])
        self.assertEqual(len({c['id'] for c in calls}), 2)
        one = '<atem:invoke name="read_file"><atem:parameter name="path">%s</atem:parameter></atem:invoke>'
        wrapped = '<atem:function_calls>\n%s\n%s\n</atem:function_calls>' % (one % 'a', one % 'b')
        calls = recover_tool_calls(wrapped, TOOLS)
        self.assertEqual([json.loads(c['function']['arguments'])['path'] for c in calls], ['a', 'b'])
        self.assertIsNone(recover_tool_calls('\n'.join(one % 'a' for _ in range(17)), TOOLS))

    def test_an_answer_is_never_a_call(self):
        call = '{"name": "run_command", "arguments": {"command": "rm -rf build"}}'
        refused = [
            f'You could run this:\n```json\n{call}\n```',                       # prose around the block
            f'<tool_call>{call}</tool_call> and then tell me what happened',
            '<tool_call>{"name": "delete_everything", "arguments": {}}</tool_call>',   # not an offered tool
            '<tool_call>{"name": "run_command", "arguments": [1, 2]}</tool_call>',
            '<tool_call>{"name": "run_command", "arguments": {"command": "ls"}</tool_call>',  # broken JSON
            '{"name": "run_command", "arguments": {"command": "ls"}, "note": "extra key"}',
            '```json\n{"package": "relay", "version": "0.1"}\n```',                # JSON that is not a call
            f'<tool_call>{call}</tool_call><tool_call>not json</tool_call>',       # one bad block spoils all
            '<tool_call><function=run_command>stray text<parameter=command>ls</parameter></function></tool_call>',
            '', '   ', None,
        ]
        atem = '<atem:invoke name="read_file"><atem:parameter name="path">a</atem:parameter></atem:invoke>'
        dsml = ('<｜DSML｜invoke name="read_file"><｜DSML｜parameter name="path" '
                'string="true">a</｜DSML｜parameter></｜DSML｜invoke>')
        refused += [
            f'I will read it:\n{atem}',                                        # prose around the block
            f'{atem}\nThen I will tell you what is in it.',
            f'{dsml} — that is the plan.',
            atem.replace('read_file', 'delete_everything'),                    # not an offered tool
            dsml.replace('read_file', 'delete_everything'),
            atem + '<atem:invoke name="read_file">stray<atem:parameter name="path">b</atem:parameter></atem:invoke>',
            atem + '<atem:invoke name="read_file"><atem:parameter name="path">b</atem:parameter>',  # never closed
            f'<tool_call>{call}</tool_call>{atem}',                            # two formats in one message
            atem + dsml,
            '<atem:function_calls></atem:function_calls>',                     # a wrapper with no call
        ]
        for content in refused:
            self.assertIsNone(recover_tool_calls(content, TOOLS), content)
        self.assertIsNone(recover_tool_calls(f'<tool_call>{call}</tool_call>', []))   # no tools offered
        many = '\n'.join(f'<tool_call>{call}</tool_call>' for _ in range(17))
        self.assertIsNone(recover_tool_calls(many, TOOLS))


class StripTests(unittest.TestCase):
    """Tool-call JSON a provider streamed as content is cut from the answer (card #VN69)."""

    MEASURED = ('The Decisions text got truncated by a stray quote — fixing the section:'
                '_resolver: fe"tool_use_id": "toolu_bdrk_01FnV1wBKtgZ6ggKyUqNuqbB"\n'
                '{\n'
                '  "type": "tool_use",\n'
                '  "id": "toolu_bdrk_01FnV1wBKtgZ6ggKyUqNuqbB",\n'
                '  "name": "board_update_card",\n'
                '  "input": {"id": "Y2JW", "section": "Decisions"}\n'
                '}\n'
                '\n'
                'Redoing the section fix properly. The decisions are on the card.')

    def test_the_measured_leak_is_cut_and_the_prose_around_it_stays(self):
        out = strip_tool_fragments(self.MEASURED)
        self.assertNotIn('toolu_bdrk', out)
        self.assertNotIn('"type": "tool_use"', out)
        self.assertNotIn('"name": "board_update_card"', out)
        self.assertIn('fixing the section:', out)          # prose before the anchor stays
        self.assertIn('Redoing the section fix properly. The decisions are on the card.', out)
        self.assertNotIn('{', out)                         # the fragment's bare opener went too

    def test_a_message_that_is_nothing_but_leaked_call_json_is_emptied(self):
        self.assertEqual(strip_tool_fragments(
            '{"type": "tool_use", "id": "toolu_1", "name": "x", "input": {}}'), '')
        self.assertEqual(strip_tool_fragments(self.MEASURED[self.MEASURED.index('{'):]
                                              .split('\n\nRedoing')[0]), '')

    def test_an_openai_envelope_leak_is_cut(self):
        out = strip_tool_fragments('Calling now.\n'
                                   '{"function": {"name": "board_read", "arguments": "{}"}},\n'
                                   'Done.')
        self.assertEqual(out, 'Calling now.\nDone.')

    def test_prose_that_merely_mentions_json_is_untouched(self):
        prose = ('The tool takes {"section": "Decisions"} as its argument, per the schema. '
                 'A JSON "type" field names the block.')
        self.assertEqual(strip_tool_fragments(prose), prose)
        self.assertEqual(strip_tool_fragments('plain answer, no JSON at all'),
                         'plain answer, no JSON at all')
        self.assertEqual(strip_tool_fragments(''), '')

    def test_a_fenced_block_stays_because_it_may_be_shown_on_purpose(self):
        fenced = 'Here is what I would send:\n```json\n{"type": "tool_use", "id": "toolu_9"}\n```'
        self.assertEqual(strip_tool_fragments(fenced), fenced)


if __name__ == '__main__':
    unittest.main()
