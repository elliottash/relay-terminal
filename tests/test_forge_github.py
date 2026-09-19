# SPDX-License-Identifier: GPL-3.0-or-later
"""The GitHub provider: REST shape, pagination, conditional requests, rate limits, credentials.

Everything runs against `tests/fake_github.py` on 127.0.0.1; nothing here reaches api.github.com,
and `resolve_token`'s subprocess steps are driven through an injected runner, so no test shells
out to `gh` or `git`.
"""
import unittest

import fake_github as FG
from relay_core import forge_github as GH
from relay_core.forge_sync import ForgeAuthError, ForgeRateLimited, ForgeUnavailable


class ProviderTest(unittest.TestCase):
    def setUp(self):
        self.gh = FG.FakeGitHub()
        self.addCleanup(self.gh.close)
        self.slept = []

    def provider(self, repo="relay/terminal", token=FG.TOKEN, **kwargs):
        kwargs.setdefault("base_url", self.gh.base_url)
        kwargs.setdefault("sleep", self.slept.append)
        return GH.GitHubProvider(repo, token, **kwargs)


# ------------------------------------------------------------------ base URLs

class BaseUrlTests(unittest.TestCase):
    def test_the_default_is_the_public_api(self):
        self.assertEqual(GH.normalize_base(None), "https://api.github.com")
        self.assertEqual(GH.normalize_base("https://api.github.com/"), "https://api.github.com")

    def test_an_enterprise_host_gets_its_rest_root(self):
        self.assertEqual(GH.normalize_base("https://ghe.example.com"), "https://ghe.example.com/api/v3")
        self.assertEqual(GH.normalize_base("ghe.example.com"), "https://ghe.example.com/api/v3")

    def test_an_explicit_path_is_left_alone(self):
        self.assertEqual(GH.normalize_base("https://ghe.example.com/api/v3/"),
                         "https://ghe.example.com/api/v3")

    def test_an_address_is_taken_as_given(self):
        self.assertEqual(GH.normalize_base("http://127.0.0.1:9000"), "http://127.0.0.1:9000")

    def test_a_non_http_base_is_refused(self):
        with self.assertRaises(ForgeUnavailable):
            GH.normalize_base("ftp://example.com")

    def test_a_bad_repo_name_is_refused_before_any_request(self):
        with self.assertRaises(ForgeUnavailable):
            GH.GitHubProvider("not-a-repo")


# ------------------------------------------------------------------- listings

class ListingTests(ProviderTest):
    def test_pull_requests_are_not_issues(self):
        self.gh.make_issue("a real issue")
        self.gh.make_issue("a pull request", pull_request=True)
        issues = self.provider().list_issues()
        self.assertEqual([i.title for i in issues], ["a real issue"])

    def test_pagination_follows_the_link_header(self):
        for n in range(7):
            self.gh.make_issue(f"issue {n}")
        self.gh.page_size = 2
        issues = self.provider().list_issues()
        self.assertEqual(len(issues), 7)
        self.assertEqual(len({i.number for i in issues}), 7)

    def test_an_unchanged_listing_answers_304_and_costs_nothing(self):
        self.gh.make_issue("one")
        provider = self.provider()
        issues = provider.list_issues()
        self.assertEqual(len(issues), 1)
        etag = provider.list_etag
        self.assertTrue(etag)
        before = len(self.gh.requests)
        self.assertIsNone(provider.list_issues(etag=etag))
        self.assertEqual(len(self.gh.requests), before + 1)      # one conditional request, no body

    def test_a_changed_listing_does_not_answer_304(self):
        self.gh.make_issue("one")
        provider = self.provider()
        provider.list_issues()
        etag = provider.list_etag
        self.gh.web_edit(1, title="one, edited")
        self.assertIsNotNone(provider.list_issues(etag=etag))

    def test_get_issue_is_conditional_too(self):
        self.gh.make_issue("one", body="hello")
        provider = self.provider()
        issue = provider.get_issue(1)
        self.assertEqual(issue.body, "hello")
        self.assertIsNone(provider.get_issue(1, etag=issue.etag))

    def test_labels_and_state_reason_come_across(self):
        self.gh.make_issue("one", labels=["bug", "tab:features"])
        self.gh.web_edit(1, state="closed", state_reason="not_planned")
        issue = self.provider().get_issue(1)
        self.assertEqual(sorted(issue.labels), ["bug", "tab:features"])
        self.assertEqual((issue.state, issue.state_reason), ("closed", "not_planned"))


# --------------------------------------------------------------------- writes

class WriteTests(ProviderTest):
    def test_create_update_and_comment(self):
        provider = self.provider()
        issue = provider.create_issue("new", "body", ["bug"], [])
        self.assertEqual(issue.number, 1)
        updated = provider.update_issue(issue.number, title="renamed", state="closed",
                                        state_reason="completed")
        self.assertEqual(updated.title, "renamed")
        self.assertEqual(updated.state, "closed")
        comment = provider.create_comment(issue.number, "hello")
        self.assertEqual(comment.body, "hello")
        edited = provider.update_comment(comment.comment_id, "hello again")
        self.assertEqual(edited.body, "hello again")
        self.assertEqual([c.body for c in provider.list_comments(issue.number)], ["hello again"])

    def test_update_with_nothing_to_change_does_not_patch(self):
        provider = self.provider()
        provider.create_issue("new", "body")
        before = list(self.gh.writes)
        provider.update_issue(1)
        self.assertEqual(self.gh.writes, before)

    def test_ensure_labels_creates_only_what_is_missing(self):
        provider = self.provider()
        self.assertEqual(sorted(provider.ensure_labels(["bug", "tab:features"])),
                         ["bug", "tab:features"])
        self.assertEqual(provider.ensure_labels(["bug"]), [])
        self.assertIn("tab:features", self.gh.main.labels)

    def test_a_missing_repository_is_a_clear_refusal(self):
        with self.assertRaises(ForgeUnavailable) as caught:
            self.provider("relay/nope").repo_info()
        self.assertIn("not found", str(caught.exception))


# ---------------------------------------------------------------- rate limits

class RateLimitTests(ProviderTest):
    def test_a_short_secondary_limit_is_waited_out(self):
        self.gh.refuse_next(403, {"Retry-After": "2"},
                            {"message": "You have exceeded a secondary rate limit"})
        self.gh.make_issue("one")
        issues = self.provider().list_issues()
        self.assertEqual(len(issues), 1)
        self.assertEqual(self.slept, [2.0])

    def test_a_long_wait_is_reported_rather_than_slept_through(self):
        self.gh.refuse_next(403, {"Retry-After": "3600"}, {"message": "secondary rate limit"})
        with self.assertRaises(ForgeRateLimited) as caught:
            self.provider(clock=lambda: 1_000_000.0).list_issues()
        self.assertEqual(self.slept, [])
        self.assertIn("try again at", str(caught.exception))
        self.assertEqual(caught.exception.retry_at, 1_000_000.0 + 3600)
        self.assertTrue(caught.exception.retry_at_text)

    def test_the_primary_limit_uses_the_reset_header(self):
        self.gh.refuse_next(403, {"x-ratelimit-remaining": "0", "x-ratelimit-reset": "1000900"},
                            {"message": "API rate limit exceeded"})
        with self.assertRaises(ForgeRateLimited) as caught:
            self.provider(clock=lambda: 1_000_000.0).list_issues()
        self.assertEqual(caught.exception.retry_at, 1000900.0)

    def test_backoff_is_bounded_and_never_loops_forever(self):
        for _ in range(6):
            self.gh.refuse_next(403, {"Retry-After": "1"}, {"message": "secondary rate limit"})
        with self.assertRaises(ForgeRateLimited):
            self.provider(max_attempts=3).list_issues()
        self.assertEqual(self.slept, [1.0, 1.0])


# ---------------------------------------------------------------- credentials

class TokenTests(unittest.TestCase):
    def test_the_explicit_argument_wins(self):
        token, source = GH.resolve_token("abc", env={"GH_TOKEN": "env"},
                                         runner=lambda *a, **k: "gh\n")
        self.assertEqual((token, source), ("abc", "argument"))

    def test_gh_token_beats_github_token(self):
        token, source = GH.resolve_token(None, env={"GH_TOKEN": "one", "GITHUB_TOKEN": "two"},
                                         runner=lambda *a, **k: "")
        self.assertEqual((token, source), ("one", "GH_TOKEN"))

    def test_github_token_is_read_when_gh_token_is_not_set(self):
        token, source = GH.resolve_token(None, env={"GITHUB_TOKEN": "two"},
                                         runner=lambda *a, **k: "")
        self.assertEqual((token, source), ("two", "GITHUB_TOKEN"))

    def test_gh_auth_token_comes_next(self):
        calls = []

        def runner(argv, stdin=None, cwd=None):
            calls.append(argv[0])
            return "from-gh\n" if argv[0] == "gh" else ""

        token, source = GH.resolve_token(None, env={}, runner=runner)
        self.assertEqual((token, source), ("from-gh", "gh auth token"))
        self.assertEqual(calls, ["gh"])

    def test_the_git_credential_helper_is_the_last_resort(self):
        def runner(argv, stdin=None, cwd=None):
            if argv[0] == "git":
                assert "host=github.com" in (stdin or "")
                return "protocol=https\nhost=github.com\nusername=x\npassword=from-git\n"
            return ""

        token, source = GH.resolve_token(None, env={}, runner=runner)
        self.assertEqual((token, source), ("from-git", "git credential"))

    def test_nothing_found_is_an_auth_error_naming_no_token(self):
        provider = GH.GitHubProvider("relay/terminal", None, env={}, runner=lambda *a, **k: "")
        with self.assertRaises(ForgeAuthError) as caught:
            provider.token()
        self.assertIn("GH_TOKEN", str(caught.exception))

    def test_nothing_resolves_a_token_at_import_time(self):
        calls = []
        GH.GitHubProvider("relay/terminal", None, env={"GH_TOKEN": "x"},
                          runner=lambda *a, **k: calls.append(a) or "")
        self.assertEqual(calls, [])              # constructing the provider asks nothing


class TokenNeverLeaksTests(ProviderTest):
    SECRET = "ghp_supersecretvalue123"

    def test_it_is_not_in_the_repr(self):
        self.assertNotIn(self.SECRET, repr(self.provider(token=self.SECRET)))

    def test_it_is_not_in_an_error_even_when_the_forge_echoes_it(self):
        self.gh.require_token = False
        self.gh.refuse_next(500, {}, {"message": f"boom with {self.SECRET} inside"})
        provider = self.provider(token=self.SECRET)
        with self.assertRaises(ForgeUnavailable) as caught:
            provider.list_issues()
        self.assertNotIn(self.SECRET, str(caught.exception))
        self.assertIn("<token>", str(caught.exception))

    def test_a_refused_credential_says_so_without_quoting_it(self):
        provider = self.provider(token=self.SECRET)
        with self.assertRaises(ForgeAuthError) as caught:
            provider.list_issues()
        self.assertNotIn(self.SECRET, str(caught.exception))

    def test_the_token_travels_only_as_the_authorization_header(self):
        self.gh.make_issue("one")
        self.provider().list_issues()
        self.assertTrue(all(a == f"Bearer {FG.TOKEN}" for a in self.gh.auth_seen))
        self.assertTrue(all(FG.TOKEN not in path for _, path in self.gh.requests))


# --------------------------------------------------------------------- forks

class ForkTests(ProviderTest):
    def test_a_fork_with_issues_off_follows_its_parent(self):
        self.gh.add_repo("upstream/terminal", has_issues=True)
        self.gh.add_repo("me/terminal", has_issues=False, parent="upstream/terminal")
        provider = GH.provider_for("me/terminal", FG.TOKEN, base_url=self.gh.base_url)
        self.assertEqual(provider.repo, "upstream/terminal")

    def test_a_fork_with_issues_on_keeps_its_own(self):
        self.gh.add_repo("upstream/terminal", has_issues=True)
        self.gh.add_repo("me/terminal", has_issues=True, parent="upstream/terminal")
        provider = GH.provider_for("me/terminal", FG.TOKEN, base_url=self.gh.base_url)
        self.assertEqual(provider.repo, "me/terminal")

    def test_a_parent_without_issues_is_not_followed(self):
        self.gh.add_repo("upstream/terminal", has_issues=False)
        self.gh.add_repo("me/terminal", has_issues=False, parent="upstream/terminal")
        provider = GH.provider_for("me/terminal", FG.TOKEN, base_url=self.gh.base_url)
        self.assertEqual(provider.repo, "me/terminal")
        self.assertFalse(provider.repo_info().has_issues)


if __name__ == "__main__":                                        # pragma: no cover
    unittest.main()
