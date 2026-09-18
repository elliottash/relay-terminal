# SPDX-License-Identifier: GPL-3.0-or-later
"""Reasoning tags and text tool calls in a local model's content (backend/relay_core/localtext.py)."""
import json
import unittest

from relay_core.localtext import ThinkSplitter, recover_tool_calls, split_reasoning


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

    def test_several_blocks_keep_their_order_and_get_distinct_ids(self):
        text = ('<tool_call>{"name": "read_file", "arguments": {"path": "a"}}</tool_call>\n'
                '<tool_call>{"name": "read_file", "arguments": {"path": "b"}}</tool_call>')
        calls = recover_tool_calls(text, TOOLS)
        self.assertEqual([json.loads(c['function']['arguments'])['path'] for c in calls], ['a', 'b'])
        self.assertEqual(len({c['id'] for c in calls}), 2)

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
        for content in refused:
            self.assertIsNone(recover_tool_calls(content, TOOLS), content)
        self.assertIsNone(recover_tool_calls(f'<tool_call>{call}</tool_call>', []))   # no tools offered
        many = '\n'.join(f'<tool_call>{call}</tool_call>' for _ in range(17))
        self.assertIsNone(recover_tool_calls(many, TOOLS))


if __name__ == '__main__':
    unittest.main()
