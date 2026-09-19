# SPDX-License-Identifier: AGPL-3.0-or-later
"""Cross-provider QA (card #T71W): the signature, the family table, the lineage and the ranking.

Every test here is pure: availability is passed in, so nothing reads PATH, the keyring or the
local-endpoint registry.  The one probe test asserts the *shape* of `availability()`, never what
happens to be installed on the machine running it.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from relay_core import guest
from relay_core import presets
from relay_core import qa_verifiers as Q

def avail(guests=(), keys=(), local=()):
    return {"installed_guests": set(guests), "keys": {k: True for k in keys},
            "local_models": local}


class SignatureTests(unittest.TestCase):
    def test_the_vendor_is_the_model_not_the_aggregator(self):
        self.assertEqual(Q.signature("openrouter", "deepseek/deepseek-v4.1-flash"),
                         "deepseek/deepseek-v4.1-flash")
        self.assertEqual(Q.signature("openrouter", "google/gemini-3.8-flash"),
                         "gemini/gemini-3.8-flash")

    def test_each_preset_signs_with_its_own_vendor(self):
        self.assertEqual(Q.signature("anthropic", "claude-opus-5"), "anthropic/claude-opus-5")
        self.assertEqual(Q.signature("openai", "gpt-6-astra"), "openai/gpt-6-astra")
        self.assertEqual(Q.signature("glm-coding", "glm-5.3"), "glm/glm-5.3")
        self.assertEqual(Q.signature("kimi-code", "k3"), "kimi/k3")
        self.assertEqual(Q.signature("gemini", "gemini-3.1-pro-preview"),
                         "gemini/gemini-3.1-pro-preview")

    def test_a_guest_signs_the_model_it_ran_and_the_harness_that_ran_it(self):
        # Owner, 2026-09-19: "lets try to record the model used."
        self.assertEqual(Q.signature("guest:claude", "claude-opus-5-20260514"),
                         "anthropic/claude-opus-5-20260514 via claude-code")
        self.assertEqual(Q.signature("guest:codex", "gpt-5.6-codex"),
                         "openai/gpt-5.6-codex via codex")
        self.assertEqual(Q.guest_signature("codex", "gpt-5.6-codex"), "openai/gpt-5.6-codex via codex")

    def test_a_guest_whose_model_cannot_be_seen_signs_the_harness_alone(self):
        self.assertEqual(Q.signature("guest:codex"), "openai/codex")
        self.assertEqual(Q.signature("guest:claude"), "anthropic/claude-code")
        self.assertEqual(Q.guest_signature("claude", "claude"), "anthropic/claude-code")

    def test_relay_free_signs_the_route_and_a_local_endpoint_signs_the_machine(self):
        self.assertEqual(Q.signature("relay-free", "relay-main"), "relay-free/relay-main")
        self.assertEqual(Q.signature("local:bonsai", "bonsai-2-27b"), "local/bonsai-2-27b")

    def test_no_preset_and_no_model_is_no_signature(self):
        self.assertEqual(Q.signature(None, None), "")
        self.assertEqual(Q.signature("", ""), "")

    def test_every_shipped_preset_and_tier_produces_a_signature_with_a_known_family(self):
        for preset_id, tiers in presets.TIER_DEFAULTS.items():
            for tier, (row_preset, model, _extra) in tiers.items():
                text = Q.signature(row_preset, model)
                self.assertTrue(text and "/" in text, (preset_id, tier, text))
                self.assertIn(Q.family(text), Q.LINEAGE, (preset_id, tier, text))


class FamilyTests(unittest.TestCase):
    def test_the_signature_and_the_legacy_free_text_land_on_one_family(self):
        # The whole point of the table: before #T71W these were "anthropic" and "claude", so each
        # could close what the other wrote.
        self.assertEqual(Q.family("anthropic/claude-opus-5"), Q.family("Claude Opus 5 (pane 2)"))
        self.assertEqual(Q.family("anthropic/claude-opus-5"), "anthropic")

    def test_the_o_series_the_codex_cli_and_gpt_are_all_openai(self):
        for text in ("gpt-5-codex", "codex", "openai/gpt-6-astra", "o3", "o4-mini", "chatgpt-4o"):
            self.assertEqual(Q.family(text), "openai", text)

    def test_the_coding_plans_are_their_vendors(self):
        self.assertEqual(Q.family("glm-coding"), "glm")
        self.assertEqual(Q.family("kimi-code"), "kimi")
        self.assertEqual(Q.family("k3"), "kimi")
        self.assertEqual(Q.family("moonshot-v1-128k"), "kimi")

    def test_an_aggregator_is_read_through_to_the_model(self):
        self.assertEqual(Q.family("openrouter/deepseek-v4.1-flash"), "deepseek")
        self.assertEqual(Q.family("openrouter/google/gemini-3.8-flash"), "gemini")

    def test_relay_free_is_the_family_of_the_gateways_upstream_for_the_role(self):
        # Not a lab: relay-main is GLM-5.3 Flash today, relay-flash DeepSeek, relay-lite Gemini.
        self.assertEqual(Q.family("relay-free/relay-main"), "glm")
        self.assertEqual(Q.family("relay-free/relay-flash"), "deepseek")
        self.assertEqual(Q.family("relay-free/relay-lite"), "gemini")

    def test_the_via_suffix_reads_as_the_model_and_falls_back_to_the_harness(self):
        self.assertEqual(Q.family("anthropic/claude-opus-5-20260514 via claude-code"), "anthropic")
        self.assertEqual(Q.family("openai/gpt-5.6-codex via codex"), "openai")
        self.assertEqual(Q.family("anthropic/claude-opus-5 via claude-code (pane 2)"), "anthropic")
        # A model id this table has never seen is still OpenAI's CLI and OpenAI's harness prompt.
        self.assertEqual(Q.family("acme/mystery-1 via codex"), "openai")
        self.assertEqual(Q.lineage(Q.family("openai/gpt-5.6-codex via codex")), "openai")

    def test_nothing_and_an_unknown_model_never_raise(self):
        self.assertEqual(Q.family(None), "")
        self.assertEqual(Q.family("   "), "")
        self.assertEqual(Q.family("acme/wonder-7"), "acme")
        self.assertNotEqual(Q.family("acme/wonder-7"), Q.family("other/wonder-7"))


class LineageTests(unittest.TestCase):
    def test_the_chinese_open_weight_models_share_one_lineage(self):
        groups = {f: Q.lineage(f) for f in ("glm", "kimi", "deepseek", "minimax", "qwen")}
        self.assertEqual(set(groups.values()), {"cn-open"})

    def test_the_three_western_labs_are_each_their_own(self):
        self.assertEqual(Q.lineage("openai"), "openai")
        self.assertEqual(Q.lineage("anthropic"), "anthropic")
        self.assertEqual(Q.lineage("gemini"), "google")

    def test_an_unknown_family_has_no_lineage_and_is_therefore_its_own(self):
        self.assertEqual(Q.lineage("acme"), "")
        self.assertEqual(Q.lineage(None), "")

    def test_every_ranked_family_has_a_lineage(self):
        for row in Q.VERIFIER_RANK:
            self.assertIn(row["family"], Q.LINEAGE, row["family"])

    def test_relay_free_is_not_a_verifier_at_all(self):
        # Owner, 2026-09-19: "relay free is never used for verifying -- so verifying is not
        # available on the free plan." It is a stated refusal, not a silent absence.
        self.assertEqual([r for r in Q.VERIFIER_RANK if r["family"] == "relay-free"], [])
        self.assertTrue(Q.is_relay_free("relay-free/relay-main"))
        self.assertFalse(Q.is_relay_free("glm/glm-5.3"))
        # …but a card *implemented* on Relay Free still resolves to its upstream's family.
        self.assertEqual(Q.family("relay-free/relay-main"), "glm")
        self.assertEqual(Q.lineage(Q.family("relay-free/relay-main")), "cn-open")


class RecommendTests(unittest.TestCase):
    def test_codex_gets_claude_when_the_cli_is_installed(self):
        result = Q.recommend("openai/codex", **avail(guests=("claude",), keys=("glm-coding",)))
        self.assertEqual(result["recommended"]["runner"], "guest:claude")
        self.assertEqual(result["recommended"]["family"], "anthropic")
        self.assertIn("outside the implementer's lineage", result["recommended"]["why"])
        self.assertEqual([s["family"] for s in result["skipped"]], ["openai"])
        self.assertNotIn("note", result)

    def test_codex_falls_to_glm_when_claude_is_not_there_and_then_to_kimi(self):
        glm = Q.recommend("openai/codex", **avail(keys=("glm-coding",)))
        self.assertEqual(glm["recommended"]["runner"], "preset:glm-coding")
        kimi = Q.recommend("openai/codex", **avail(keys=("kimi-code",)))
        self.assertEqual(kimi["recommended"]["runner"], "preset:kimi-code")
        deepseek = Q.recommend("openai/codex", **avail(keys=("openrouter",)))
        self.assertEqual(deepseek["recommended"]["family"], "deepseek")

    def test_claude_gets_codex_first(self):
        result = Q.recommend("anthropic/claude-opus-5",
                             **avail(guests=("codex", "claude"), keys=("glm-coding",)))
        self.assertEqual(result["recommended"]["runner"], "guest:codex")
        self.assertEqual(result["recommended"]["label"], "Codex")
        self.assertEqual([s["family"] for s in result["skipped"]], ["anthropic"])

    def test_the_legacy_free_text_implementer_is_skipped_just_as_the_signature_is(self):
        result = Q.recommend("Claude Opus 5 (pane 2)", **avail(guests=("codex", "claude")))
        self.assertEqual(result["recommended"]["runner"], "guest:codex")
        self.assertEqual([s["family"] for s in result["skipped"]], ["anthropic"])

    def test_a_card_implemented_on_relay_free_is_verified_outside_its_upstreams_lineage(self):
        # relay-main is GLM-5.3 Flash today, so the card is a cn-open card: GLM is skipped and the
        # rest of cn-open goes behind every other lineage.
        result = Q.recommend("relay-free/relay-main",
                             **avail(guests=("codex",), keys=("kimi-code", "gemini")))
        self.assertEqual(result["implementer_family"], "glm")
        self.assertEqual(result["recommended"]["runner"], "guest:codex")
        self.assertEqual([a["family"] for a in result["alternates"]], ["gemini", "kimi"])
        self.assertEqual([s["family"] for s in result["skipped"]], ["glm"])

    def test_glm_gets_a_different_lineage_first_and_its_own_lineage_last(self):
        result = Q.recommend("glm/glm-5.3", **avail(guests=("codex", "claude"),
                                                    keys=("kimi-code", "openrouter", "gemini")))
        self.assertEqual(result["recommended"]["runner"], "guest:codex")
        families = [a["family"] for a in result["alternates"]]
        self.assertEqual(families[:2], ["anthropic", "gemini"])
        # Kimi and DeepSeek are cn-open like the implementer: still offered, but behind everything.
        self.assertEqual(families[-2:], ["kimi", "deepseek"])
        for entry in result["alternates"][-2:]:
            self.assertIn("same lineage as the implementer (cn-open)", entry["why"])

    def test_a_machine_with_only_relay_free_has_no_verifier_and_is_told_what_to_add(self):
        result = Q.recommend("anthropic/claude-opus-5", **avail())
        self.assertIsNone(result["recommended"])
        self.assertEqual(result["alternates"], [])
        self.assertEqual(result["note"], Q.NO_VERIFIER_NOTE)
        self.assertIn("add a provider key", result["note"])
        self.assertEqual([u for u in result["unavailable"] if u["family"] == "relay-free"],
                         [dict(Q.RELAY_FREE_ROW)])
        self.assertTrue([u for u in result["unavailable"] if u["family"] == "openai"])

    def test_relay_free_is_reported_unavailable_even_when_a_verifier_was_found(self):
        result = Q.recommend("anthropic/claude-opus-5", **avail(guests=("codex",)))
        self.assertEqual(result["recommended"]["runner"], "guest:codex")
        self.assertIn(dict(Q.RELAY_FREE_ROW), result["unavailable"])
        self.assertNotIn("note", result)

    def test_an_unavailable_family_says_what_is_missing(self):
        result = Q.recommend("glm/glm-5.3", **avail())
        reasons = {u["family"]: u["why"] for u in result["unavailable"]}
        self.assertEqual(reasons["openai"], "codex not on PATH, no openai key")
        self.assertEqual(reasons["kimi"], "no key")

    def test_a_same_lineage_recommendation_says_so_and_carries_a_note(self):
        result = Q.recommend("glm/glm-5.3", **avail(keys=("kimi-code",)))
        self.assertEqual(result["recommended"]["family"], "kimi")
        self.assertTrue(result["recommended"]["same_lineage"])
        self.assertIn("same lineage", result["recommended"]["why"])
        self.assertIn("cn-open", result["note"])

    def test_a_local_endpoint_is_offered_last_and_carries_its_own_note(self):
        local = (("local:bonsai", "bonsai-2-27b", "Bonsai 2 27B"),)
        result = Q.recommend("anthropic/claude-opus-5", **avail(guests=("codex",), local=local))
        self.assertEqual(result["recommended"]["runner"], "guest:codex")
        self.assertEqual(result["alternates"][-1]["family"], "local")
        self.assertEqual(result["alternates"][-1]["model"], "bonsai-2-27b")
        only_local = Q.recommend("anthropic/claude-opus-5", **avail(local=local))
        self.assertEqual(only_local["recommended"]["runner"], "preset:local:bonsai")
        self.assertIn("capability floor", only_local["note"])

    def test_an_unknown_implementer_takes_the_top_of_the_ranking_and_says_why(self):
        result = Q.recommend("", **avail(guests=("codex", "claude")))
        self.assertEqual(result["recommended"]["runner"], "guest:codex")
        self.assertIn("implementer is unknown", result["recommended"]["why"])
        self.assertEqual(result["skipped"], [])
        self.assertEqual(result["implementer_family"], "")

    def test_nothing_at_all_available_recommends_nobody_rather_than_guessing(self):
        result = Q.recommend("openai/codex", **avail())
        self.assertIsNone(result["recommended"])
        self.assertEqual(result["alternates"], [])
        self.assertTrue(result["unavailable"])

    def test_each_runner_carries_the_model_it_would_actually_run(self):
        result = Q.recommend("anthropic/claude-opus-5",
                             **avail(guests=("codex",), keys=("glm-coding", "openrouter")))
        models = {entry["runner"]: entry["model"]
                  for entry in [result["recommended"], *result["alternates"]]}
        self.assertEqual(models["guest:codex"], "codex")
        self.assertEqual(models["preset:glm-coding"], presets.TIER_DEFAULTS["glm-coding"]["main"][1])
        self.assertEqual(models["preset:openrouter"], presets.TIER_DEFAULTS["openrouter"]["main"][1])

    def test_the_summary_line_reads_as_one_sentence(self):
        line = Q.summary_line(Q.recommend("anthropic/claude-opus-5",
                                          **avail(guests=("codex",), keys=("glm-coding",))), "K7Q2")
        self.assertTrue(line.startswith("Verify #K7Q2 with Codex (installed)"))
        self.assertIn("then GLM-5.3 (key)", line)
        self.assertIn("skipped Claude: implemented this card", line)


class CommitTrailerTests(unittest.TestCase):
    """A real, tiny git repository: the trailers are read with git, so a fake would prove nothing."""

    def setUp(self):
        if not self._git_available():
            self.skipTest("git is not installed")
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)
        env = {**os.environ, "GIT_AUTHOR_NAME": "T", "GIT_AUTHOR_EMAIL": "t@example.invalid",
               "GIT_COMMITTER_NAME": "T", "GIT_COMMITTER_EMAIL": "t@example.invalid",
               "GIT_CONFIG_GLOBAL": str(self.repo / "gitconfig"), "GIT_CONFIG_SYSTEM": "/dev/null"}
        self.env = env
        self.git(["init", "-q", "-b", "main", "."])
        (self.repo / "a.txt").write_text("one\n", encoding="utf-8")
        self.git(["add", "a.txt"])
        self.git(["commit", "-q", "-m",
                  "Do the thing (#K7Q2)\n\nImplemented-By: anthropic/claude-opus-5\n"])
        self.first = self.git(["rev-parse", "--short", "HEAD"]).strip()
        (self.repo / "a.txt").write_text("two\n", encoding="utf-8")
        self.git(["commit", "-q", "-am", "Follow-up on #K7Q2\n\nImplemented-By: openai/codex\n"])
        self.second = self.git(["rev-parse", "--short", "HEAD"]).strip()

    @staticmethod
    def _git_available() -> bool:
        try:
            subprocess.run(["git", "--version"], capture_output=True, timeout=10)
        except (OSError, subprocess.SubprocessError):      # pragma: no cover - git is always here
            return False
        return True

    def git(self, args):
        done = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True, text=True,
                              env=self.env, timeout=30)
        self.assertEqual(done.returncode, 0, done.stderr)
        return done.stdout

    def test_a_trailer_that_matches_the_card_agrees_and_one_that_does_not_says_so(self):
        rows = Q.commit_trailers(self.repo, [self.first, self.second], "anthropic/claude-opus-5")
        self.assertEqual([r["trailer"] for r in rows],
                         ["anthropic/claude-opus-5", "openai/codex"])
        self.assertEqual([r["agrees"] for r in rows], [True, False])

    def test_the_commits_of_a_card_are_found_by_its_id(self):
        found = Q.commits_for(self.repo, "K7Q2")
        self.assertEqual(len(found), 2)
        rows = Q.card_commits(self.repo, "#k7q2", {"commits": [self.first]},
                              "anthropic/claude-opus-5")
        self.assertEqual(rows[0]["hash"], self.first)      # links.commits first, then the search
        self.assertEqual(len(rows), 2)                     # and the same commit is not listed twice

    def test_a_missing_repo_a_bad_hash_and_no_card_are_answers_not_exceptions(self):
        self.assertEqual(Q.commit_trailers(self.repo / "nope", ["deadbee"], "x"),
                         [{"hash": "deadbee", "trailer": "", "agrees": None}])
        self.assertEqual(Q.commit_trailers(self.repo, [], "x"), [])
        self.assertEqual(Q.commits_for(self.repo, ""), [])
        self.assertEqual(Q.commits_for(self.repo / "nope", "K7Q2"), [])


class AvailabilityTests(unittest.TestCase):
    def test_the_probe_answers_the_four_keys_and_only_known_guests(self):
        found = Q.availability(cache_seconds=0)
        self.assertEqual(set(found), {"installed_guests", "keys", "local_models"})
        self.assertTrue(found["installed_guests"] <= set(guest.guest_ids()))
        # Every preset has a row, and with RELAY_KEYRING=off (scripts/test.sh) none is a real key.
        self.assertEqual(set(found["keys"]), set(presets.PRESETS))

    def test_recommend_here_is_recommend_with_this_machines_availability(self):
        here = Q.recommend_here("openai/codex")
        self.assertEqual(here["implementer_family"], "openai")
        self.assertEqual([s["family"] for s in here["skipped"]], ["openai"])
