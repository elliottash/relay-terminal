"""Board selection must name the provider serving an existing card (#BMS1)."""
import copy
from types import SimpleNamespace
from unittest.mock import patch

from test_board_protocol import ProtocolTest
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig


class CardModelSelectionTests(ProtocolTest):
    def setUp(self):
        super().setUp()
        self.main = Agent(ProviderConfig(api_key='test', base_url='https://example.invalid',
                                         model='kimi-k3'), str(self.repo), self.events.append)
        self.turns.agent = self.main
        self.commands.console = True

    def test_cached_card_changes_provider_and_keeps_history(self):
        card = Agent(copy.deepcopy(self.main.config), str(self.repo), self.events.append)
        card.messages.append({'role': 'user', 'content': 'Remember this conversation'})
        old_provider = card.provider
        session = SimpleNamespace(agent=card, idle=True)
        new = ProviderConfig(api_key='test', base_url='https://other.invalid', model='new-model')
        self.main.set_model(new)
        self.commands._sync_card_model(session)
        self.assertEqual(card.config.model, 'new-model')
        self.assertEqual(card.provider.config.base_url, 'https://other.invalid')
        self.assertIsNot(card.provider, old_provider)
        self.assertIsNot(card.config, self.main.config)
        self.assertIn('Remember this conversation', str(card.messages))
        same_provider = card.provider
        self.commands._sync_card_model(session)
        self.assertIs(card.provider, same_provider)

    def test_guest_pick_is_refused_with_actual_model_before_launch(self):
        with patch('relay_core.guest_harness_provider.start_provider') as start:
            self.assertTrue(self.commands.refuse_model_selection(
                {'preset': 'guest:codex', 'model': 'gpt-6-astra', 'id': 'pick'}))
        start.assert_not_called()
        event = self.of('model_switch_refused')[-1]
        self.assertEqual(event['current_model'], 'kimi-k3')
        self.assertIn('cannot run', event['reason'])
        self.assertEqual(event['code'], 'board_model_unavailable')
        self.assertEqual(self.main.config.model, 'kimi-k3')

    def test_guest_role_pick_is_also_refused(self):
        config = ProviderConfig(api_key='', base_url='harness://codex', model='gpt-6-astra')
        self.assertTrue(self.commands.refuse_model_selection({'role': 'high'}, config))

    def test_busy_or_queued_card_refuses_model_selection(self):
        with patch.object(self.commands.cards, 'idle', return_value=False):
            self.assertTrue(self.commands.refuse_model_selection({'preset': 'kimi'}))
        self.assertIn('queued', self.of('model_switch_refused')[-1]['reason'])
        self.assertFalse(self.commands.refuse_model_selection({'preset': 'kimi'}))

    def test_terminal_guest_switch_is_not_intercepted(self):
        self.commands.console = False
        self.assertFalse(self.commands.refuse_model_selection({'preset': 'guest:codex'}))

    def test_running_card_is_not_mutated(self):
        card = SimpleNamespace(config='untouched')
        self.commands._sync_card_model(SimpleNamespace(agent=card, idle=False))
        self.assertEqual(card.config, 'untouched')

    def test_two_real_card_turns_use_selected_provider(self):
        import time
        from relay_core.board_protocol import BoardCommands
        from test_images import RecordingProvider
        # Undo ProtocolTest's fake builder: run the actual card construction and ask path.
        self.commands._build_card_console = BoardCommands._build_card_console.__get__(self.commands)
        self.main.completion_check = False
        RecordingProvider.served = []
        with patch('relay_core.agent.ChatProvider', RecordingProvider):
            card_id = self.make_card()
            def ask(text):
                self.commands.dispatch({'type': 'board_ask', 'card': card_id, 'text': text})
                deadline = time.monotonic() + 5
                while not self.commands.cards.idle() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue(self.commands.cards.idle())
            ask('Remember the first question')
            agent = self.commands.cards.session(card_id).agent
            self.main.set_model(ProviderConfig(api_key='test', base_url='https://other.invalid',
                                               model='selected-model'))
            ask('What was the first question?')
            self.assertIs(self.commands.cards.session(card_id).agent, agent)
            self.assertEqual([model for model, _ in RecordingProvider.served],
                             ['kimi-k3', 'selected-model'])
            self.assertIn('Remember the first question', str(RecordingProvider.raw[-1]))
