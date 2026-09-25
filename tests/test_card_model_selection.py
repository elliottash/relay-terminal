"""Board selection must name the provider serving an existing card (#BMS1)."""
import copy
from types import SimpleNamespace
from unittest.mock import Mock, patch

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

    def test_guest_pick_is_allowed_now_that_the_helper_can_run_on_one(self):
        """#E34S: the guest process is the helper's own agent, so a guest pick in its model box
        is a real selection — a card console follows the priority list instead, not the pick."""
        with patch('relay_core.guest_harness_provider.start_provider') as start:
            self.assertFalse(self.commands.refuse_model_selection(
                {'preset': 'guest:codex', 'model': 'gpt-6-astra', 'id': 'pick'}))
        start.assert_not_called()          # launching the guest was never this event's job
        self.assertFalse(self.of('model_switch_refused'))
        self.assertEqual(self.main.config.model, 'kimi-k3')

    def test_guest_pick_still_refuses_while_cards_are_running(self):
        """The #BMS1 rule stands: a card console cannot swap models mid-turn."""
        with patch.object(self.commands.cards, 'idle', return_value=False):
            self.assertTrue(self.commands.refuse_model_selection(
                {'preset': 'guest:codex', 'model': 'gpt-6-astra', 'id': 'pick'}))
        event = self.of('model_switch_refused')[-1]
        self.assertEqual(event['current_model'], 'kimi-k3')
        self.assertIn('queued', event['reason'])
        self.assertEqual(event['code'], 'board_model_unavailable')
        self.assertEqual(self.main.config.model, 'kimi-k3')

    def guest_main(self, keys=('kimi',)):
        """The helper agent as #E34S configures it: its own agent is the guest, and the
        resolver's Main list is the Options › Models priority list the worker was sent. The
        agent is built on an injected provider, as the worker builds it — a `harness://`
        config is not an endpoint, so nothing may validate one."""
        from relay_core.roles import RoleResolver
        config = ProviderConfig(api_key='', base_url='harness://claude', model='claude-fake')
        store = {name: 'test' for name in keys}
        self.main = Agent(config, str(self.repo), self.events.append, provider=Mock(),
                          track_requests=False)
        self.main.roles = RoleResolver(config, 'guest:claude', None,
                                       key_lookup=lambda pid: store.get(pid, ''),
                                       tiers={'main': [{'preset': 'kimi', 'model': 'kimi-k3'}]},
                                       guest_check=lambda guest_id: True)
        self.turns.agent = self.main

    def test_a_card_console_takes_the_priority_list_when_the_helper_runs_on_a_guest(self):
        self.guest_main()
        config, preset, effort = self.commands._console_model(self.main)
        self.assertEqual(preset, 'kimi')
        self.assertEqual(config.model, 'kimi-k3')
        self.assertEqual(config.base_url, 'https://api.moonshot.ai/v1')

    def test_with_nothing_on_the_list_the_console_gets_the_refusal_sentence(self):
        self.guest_main(keys=())
        with self.assertRaises(ValueError) as raised:
            self.commands._console_model(self.main)
        self.assertIn('cannot be a second', str(raised.exception))

    def test_a_cached_card_moves_to_the_spare_model_and_keeps_history(self):
        self.guest_main()
        card = Agent(ProviderConfig(api_key='test', base_url='https://example.invalid',
                                    model='old-model'), str(self.repo), self.events.append)
        card.messages.append({'role': 'user', 'content': 'Remember this conversation'})
        old_provider = card.provider
        self.commands._sync_card_model(SimpleNamespace(agent=card, idle=True))
        self.assertEqual(card.config.model, 'kimi-k3')
        self.assertEqual(card.config.base_url, 'https://api.moonshot.ai/v1')
        self.assertIsNot(card.provider, old_provider)
        self.assertIn('Remember this conversation', str(card.messages))

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
