# Priority budgets and research pipelines: sources and design notes for a global project board

Sourced research report for Elliott Ash's global project board: how to prioritize and budget across many concurrent projects (including AI-agent budgets), and how academics track a paper pipeline. All URLs were verified live at the time of writing.

## Part 1 — Prioritization and budgeting across many concurrent projects

### Kanban portfolio practice

Kanban is the best-documented practice for one person or team running many workstreams at once. The Kanban Guide's core practice is *limiting work in progress* (WIP): cap how many items are "active," and pull a new one only when an old one finishes, which raises flow efficiency and predictability ([kanbanguides.org](https://kanbanguides.org/english/)). The same guide defines flow metrics — throughput, WIP, work-item age, and cycle time — which map directly onto "how many papers are in-flight, and how long has each been stuck at this stage?" At the portfolio level the same idea applies: instead of a per-board WIP limit, set a per-person limit across all projects ("at most 3 papers in active Analysis/Sliding at once"). The kanban-community maxim for this is "stop starting, start finishing" — finish before pulling — discussed in standard introductions to WIP limits ([LeanKit/Planview, "What is a WIP limit?"](https://leankit.com/learn/kanban/what-is-wip-limit/)). The personal-scale version is *Personal Kanban* (Benson & Barry), which boils the whole system down to "visualize your work, limit your WIP" for individuals ([personalkanban.com](https://personalkanban.com/)).

### WSJF / cost of delay

Scaled Agile Framework's **Weighted Shortest Job First** orders work by Cost of Delay (user value + time criticality + risk/opportunity) divided by job size, so small, urgent, high-value items go first ([scaledagileframework.com/wsjf](https://www.scaledagileframework.com/wsjf/)). WSJF was designed for teams choosing between features, but it transfers to an individual's paper portfolio if you read "Cost of Delay" as *value lost per week of waiting*: a grant co-funding a paper before a deadline, an R&R whose journal clock is running, a paper whose novelty decays as competitors publish. Its main benefit for an individual is that it makes the *cost of parking* a project explicit — exactly what a board with 60 on-hold papers needs. Its weakness at individual scale: CoD estimates are subjective and re-estimation churn can become its own overhead; use it coarsely (e.g., quarterly re-scoring of the on-hold list), not as a weekly ritual.

### Scoring frameworks: Eisenhower, MoSCoW, RICE, ICE

- **Eisenhower matrix** (urgent/important): for triage of the weekly queue, not for ranking papers against each other ([eisenhower.me](https://www.eisenhower.me/eisenhower-matrix/)).
- **MoSCoW** (Must/Should/Could/Won't): a lightweight criticality tag — "Must this quarter" vs "Could" — well suited as the board's `criticality` field because it forces an explicit *Won't* column, i.e., a place where on-hold papers live honestly ([MoSCoW method, Wikipedia](https://en.wikipedia.org/wiki/MoSCoW_method)).
- **RICE** (Reach × Impact × Confidence / Effort) and **ICE** (Impact × Confidence × Ease): quick multiplication scores designed for product teams ([Intercom's RICE guide](https://www.intercom.com/blog/rice/); [ProductPlan ICE glossary](https://www.productplan.com/glossary/ice-scoring-model/)). For papers, "Reach" becomes expected citations/placement, "Effort" becomes remaining person-hours. Their real value for one person: they force a *denominator* (effort to completion), which is the best counterweight to the lure of shiny new projects.

### OKR-style "3 priorities"

John Doerr's guidance is to keep objectives to roughly 3–5, because focus dies above that ([whatmatters.com, "How many OKRs should you have?"](https://www.whatmatters.com/faqs/how-many-okrs-to-have)). Applied to the board: allow a *small* number of "critical" tags (e.g., max 3 papers + 1 grant + 1 software project per quarter), enforced by the tool, so the tag can't silently inflate.

### Cal Newport: slow productivity

Newport's *Slow Productivity* prescribes "do fewer things, work at a natural pace, obsess over quality," and operationally argues for one (or at most two) big projects at a time plus an idea backlog that projects are pulled from only when a slot opens ([calnewport.com/slow-productivity](https://calnewport.com/slow-productivity/)). This is the individual-level counterpart of WIP limits and the strongest published rationale for a hard cap on Elliott's active paper count.

### Oliver Burkeman: open list / closed list

Burkeman's *Four Thousand Weeks* and his newsletter *The Imperfectionist* introduce the **open list vs. closed list** distinction: an open list accepts unlimited new items (and therefore grows unboundedly); a closed list is fixed-size — you can only add an item by finishing or deleting one ([oliverburkeman.com/fourthousandweeks](https://www.oliverburkeman.com/fourthousandweeks); [imperfectionist.substack.com](https://imperfectionist.substack.com/)). For the board this is the design pattern for the "active" bucket: 30 active papers is arguably not a closed list; the board should visualize active capacity as a *slot count*, not an infinite column.

### Timeboxing and time budgets per project

Time-blocking/timeboxing — allocating calendar blocks to projects in advance — is the mechanism that turns a budget into behavior ([Todoist's time-blocking guide](https://todoist.com/productivity-methods/time-blocking)). The time-tracking category has converged on **project budgets with burn alerts**:

- **Toggl Track**: per-project budgets and "budget alert thresholds for all projects" with emailed alerts as spend approaches the cap ([toggl.com/track/pricing](https://toggl.com/track/pricing/)).
- **Clockify**: explicit project budgets tracked as hours or money against estimates ([clockify.me/help/projects/project-budget](https://clockify.me/help/projects/project-budget)).
- **Timewarrior**: CLI-native tracking of time by project/tags ([timewarrior.net/docs](https://timewarrior.net/docs/)) — good fit for a terminal-centric board; budgets would be computed from intervals rather than natively enforced.
- **Timing.app**: automatic macOS time attribution per app/file, useful as the passive "actuals" feed for per-project hours ([timingapp.com/features](https://timingapp.com/features)).

The pattern to copy: budget = target hours per period; burn = logged hours; alert thresholds (e.g., 50%, 80%, 100%) displayed on the board row.

### Personal portfolio management for academics

- **Radhika Nagpal, "The Awesomest 7-year Postdoc"** (Scientific American): tenure-track survival as a portfolio problem — decide what your job is (research), protect deep-work hours, and treat the rest as commitments to be bounded ([blogs.scientificamerican.com](https://blogs.scientificamerican.com/guest-blog/the-awesomest-7-year-postdoc-or-how-i-learned-to-stop-worrying-and-love-the-tenure-track-faculty-life/)).
- **Karen Kelsky, *The Professor Is In***: the canonical tenure-track advice on saying no — every yes consumes finite dissertation/paper hours ([book page](https://www.penguinrandomhouse.com/books/533610/the-professor-is-in-by-karen-kelsky-ph-d/)).
- **Paul Silvia, *How to Write a Lot***: writing productivity comes from a scheduled, defended time budget (a few hours most days), not from binge sessions — i.e., a *recurring per-project hours budget* rather than heroic sprints ([APA book page](https://www.apa.org/pubs/books/How-Write-Lot)).
- **Anne Lamott's "shitty first drafts"** (*Bird by Bird*): drafts are meant to be bad; the pipeline should not treat "Rough Draft" as a quality gate but as a throughput stage — many boards over-index on polish too early ([quote](https://www.goodreads.com/quotes/702365-shitty-first-drafts)).

### Multitasking cost — why budgets at all

The empirical rationale for per-person WIP caps and time budgets:

- **Gloria Mark et al., "The Cost of Interrupted Work"** (CHI 2008): interrupted knowledge work is completed faster but at measurably higher stress and frustration; her later work documents extremely short average attention episodes on screens ([ACM DOI](https://dl.acm.org/doi/10.1145/1357054.1357072); [*Attention Span*](https://www.penguinrandomhouse.com/books/700897/attention-span-by-gloria-mark/)).
- **Sophie Leroy, "Why is it so hard to do my work?"** (JOM 2009): **attention residue** — part of your attention stays stuck on the previous task after switching, degrading performance on the next task ([DOI](https://journals.sagepub.com/doi/10.1177/0149206309335164)).

Implication for the board: every additional *active* paper imposes a switching tax on every other paper; the budget UI should show total active-load (sum of in-flight stages), not just a list.

### AI-agent budgets: how tools cap agent spend today

Current products cap spend at the *account/workspace/key* level, mostly **not per project**:

- **Claude Code**: usage limits per plan (5-hour and weekly windows) and a costs doc covering token tracking and team spend limits ([docs.claude.com/en/docs/claude-code/costs](https://docs.claude.com/en/docs/claude-code/costs); [support.anthropic.com — Claude Code plans](https://support.anthropic.com/en/articles/11145838-using-claude-code-with-your-pro-team-or-max-plan)).
- **Anthropic Console**: workspaces plus account **monthly spend limits** and usage dashboards ([platform.claude.com/settings/limits](https://platform.claude.com/settings/limits); [platform.claude.com/usage](https://platform.claude.com/usage)).
- **OpenAI Codex**: plan-based usage limits and token-based pricing for API use ([developers.openai.com/codex/pricing](https://developers.openai.com/codex/pricing)).
- **Cursor**: per-account usage tracking of included requests and usage-based spend ([docs.cursor.com/account/usage](https://docs.cursor.com/account/usage)).
- **OpenRouter**: per-API-key **credit limits** — create one key per project and each project gets a hard dollar cap ([openrouter.ai/docs/api-reference/limits](https://openrouter.ai/docs/api-reference/limits)). This is the closest existing implementation of "per-project token budget."
- **Helicone** (LLM observability proxy): tag requests with **custom properties** (e.g., `project=paper-042`) and group cost by property, plus threshold **alerts** ([custom properties](https://docs.helicone.ai/features/advanced-usage/custom-properties); [alerts](https://docs.helicone.ai/features/alerts)).
- **LangSmith**: cost/usage analytics grouped by **project** ([docs.smith.langchain.com — projects](https://docs.smith.langchain.com/observability/concepts/projects)); **Langfuse** similarly tracks cost per trace/project in open source ([langfuse.com/docs](https://langfuse.com/docs)).

**Gap**: no mainstream tool yet offers per-project agent budgets with a *burn-down visualization* (allocated vs. consumed vs. projected) of the kind Toggl/Clockify offer for human hours. The board's `agent budget` field is therefore a genuine product opportunity: allocate dollars/tokens per project per month, meter via proxy tagging (Helicone-style properties or per-project OpenRouter keys), and render burn-down bars next to the hours budget.

## Part 2 — How academics and labs track a research paper pipeline

### Tools and templates actually used

- **Generic project tools repurposed**: Notion template gallery ([notion.com/templates](https://www.notion.com/templates)), Airtable template gallery ([airtable.com/templates](https://www.airtable.com/templates)), and Trello research inspiration boards ([trello.com/inspiration/research](https://trello.com/inspiration/research)) all host "research pipeline"/"paper tracker" boards built by academics; the dominant homemade format remains a Google Sheet with one row per paper and a status column.
- **OSF** ([osf.io](https://osf.io/)) is the social-science-native home for project materials and registrations, but tracks artifacts per project, not the portfolio across projects — the board complements it.
- Wet-lab ELN/LIMS systems — LabArchives ([labarchives.com](https://www.labarchives.com/)), Benchling ([benchling.com](https://www.benchling.com/)) — do have experiment-level states and provenance, but are peripheral for social science/CS; the transferable idea is a *timestamped activity log per project*.

### Stage gates for papers

Stage-gate thinking originates in product development (Cooper's Stage-Gate; [stage-gate.com](https://www.stage-gate.com/)): work proceeds through gates with explicit entry criteria, and the pipeline's health is judged by how long items sit between gates. Academics implicitly use the same gates — idea → analysis → draft → working paper → submission → under review → R&R → published — as seen in the "paper tracker" columns of the Notion/Airtable/Trello templates above and in advice like Hal Varian's *How to Build an Economic Model in Your Spare Time*, which frames research as progressing an idea through a disciplined sequence of artifacts ([PDF](https://people.ischool.berkeley.edu/~hal/Papers/how.pdf)). The design lesson: gates need *entry criteria* (e.g., a paper may not enter "Rough Draft" until the identification strategy is written down), otherwise stages become vibes.

### Ball-in-court and staleness

"Whose turn is it" is the field academics most often track by memory and lose. The general pattern is GTD's **Waiting For** list — every delegated item carries an owner and a date ([Getting Things Done, Wikipedia](https://en.wikipedia.org/wiki/Getting_Things_Done)). Co-author norms of the form "the person who last touched the draft owns the next revision" are standard advice in writing-together guides (Varian, above; Cochrane's *Writing Tips for PhD Students*, which pushes authors to move papers forward on explicit schedules, [johnhcochrane.com](https://www.johnhcochrane.com/research-all/writing-tips-for-phd-studentsnbsp)). Litigation practice management makes "ball in court" a first-class field — apt for a law professor. **Staleness** signals that appear in real trackers: *last edited date* of the draft, *last co-author touch* (email/meeting), and *days since submission/decision* versus the journal's stated turnaround. Gelman's annual "what we did in 2020"-style portfolio reviews are a published example of forcing-function reviews across dozens of concurrent projects ([statmodeling.stat.columbia.edu](https://statmodeling.stat.columbia.edu/2021/01/01/what-we-did-in-2020/)); his post-mortems on failed papers ([statmodeling.stat.columbia.edu](https://statmodeling.stat.columbia.edu/2025/10/09/the-worst-papers-ive-ever-written/)) illustrate why on-hold projects should periodically be *killed or revived deliberately*, not just left to rot.

### Managing 20+ papers at once — published advice

The consistent themes across Nagpal, Silvia, Kelsky, Newport, Varian, and Gelman: (1) few truly active projects at a time, everything else explicitly parked; (2) a recurring, defended writing budget; (3) stages with fast hand-offs — a "shitty first draft" early so co-authors have something to react to; (4) periodic whole-portfolio reviews rather than per-project firefighting; (5) willingness to retire projects whose cost of delay has collapsed (scoop risk, funding lapse, co-author exit). Nobody credible recommends 30 genuinely active papers — but Gelman's practice shows a *wide* funnel is fine if the active band is narrow.

### Fields that real trackers use (extracted from the templates/tools above)

title, project/paper type, status/stage (free-text or enum), priority/criticality, co-authors, next action + its owner ("ball in court"), last activity/touch date, target journal, deadlines (conference, grant, R&R due date), links (Overleaf, repo, OSF, submission portal), effort estimate or hours logged, and notes/blockers.

## (a) Recommended field set for a per-project record

| Field | Rationale (source) |
|---|---|
| `name` / `type` (paper, editing, grant, teaching, software) | Cross-domain board needs type to bucket WIP limits per domain (Kanban Guide). |
| `stage` (0 Planning … 9 2nd R&R → published) | Stage-gate discipline: flow metrics only work with explicit stages ([kanbanguides.org](https://kanbanguides.org/english/); [stage-gate.com](https://www.stage-gate.com/)). |
| `criticality` (MoSCoW, max 3 "Must") | Forces an explicit Won't; OKR-style cap on true priorities ([Wikipedia](https://en.wikipedia.org/wiki/MoSCoW_method); [whatmatters.com](https://www.whatmatters.com/faqs/how-many-okrs-to-have)). |
| `my-hours-per-week budget` (+ burn %) | Silvia's scheduled writing hours + Toggl/Clockify budget-with-alerts pattern ([APA](https://www.apa.org/pubs/books/How-Write-Lot); [toggl.com/track/pricing](https://toggl.com/track/pricing/)). |
| `agent budget` (tokens/$, + burn %) | Closest precedents: OpenRouter per-key credit caps and Helicone per-property cost tracking; the burn-down UI is the gap to fill ([openrouter.ai](https://openrouter.ai/docs/api-reference/limits); [docs.helicone.ai](https://docs.helicone.ai/features/advanced-usage/custom-properties)). |
| `ball-in-court` (person + date handed off) | GTD "Waiting For" list; prevents silent stalls in co-authored work ([Wikipedia GTD](https://en.wikipedia.org/wiki/Getting_Things_Done)). |
| `next action` (one concrete verb-object) | GTD next-action discipline; stage gates need a defined "what moves this forward." |
| `staleness` (last touch; auto-days-since) | Flow metric "work-item age" ([kanbanguides.org](https://kanbanguides.org/english/)); Gelman-style periodic portfolio reviews need it ([statmodeling](https://statmodeling.stat.columbia.edu/2021/01/01/what-we-did-in-2020/)). |
| `deadline` (+ whose clock: mine / journal's / grant's) | Cost of Delay's time-criticality term; R&R and grant clocks are externally imposed ([SAFe WSJF](https://www.scaledagileframework.com/wsjf/)). |
| `co-authors` (+ their roles) | Tracker field set above; needed to interpret ball-in-court. |
| `links` (Overleaf, repo, OSF, portal) | Single row = single source of pointers; mirrors OSF/Airtable template practice. |

## (b) Weekly review rules for the board

1. **Read the funnel, not the list**: sort by stage and check WIP per stage; any stage over its cap (e.g., >3 in Analysis) means *finish before pulling* (Kanban Guide).
2. **Count active load**: total active papers × switching tax — if active count crept up, something must be parked, per Newport and attention-residue research (Leroy 2009).
3. **Enforce the criticality cap**: at most ~3 "Must" projects; demote before promoting (Doerr; Burkeman's closed list).
4. **Sweep staleness**: anything untouched >14 days with no external clock gets a decision — touch, park, or kill (Gelman-style review).
5. **Check every ball-in-court older than 7 days**: nudge, reclaim, or reassign (GTD Waiting For).
6. **Budget burn check**: flag projects >80% of weekly hours budget or monthly agent budget; over-burn means either the estimate was wrong or the project is crowding out "Must" items (Toggl/Clockify alert pattern).
7. **Cost-of-delay triage on deadlines**: re-sort by whose clock is running (journal R&R clock > my preference) before choosing the week's focus (WSJF).
8. **Advance every active paper by one stage-action**: the week is a failure if a "Must" paper ended with the same `next action` it started with (Silvia: small scheduled progress beats sprints).
9. **Retire deliberately**: pick the weakest on-hold project quarterly and either revive it with a concrete next action or kill it — parked lists rot otherwise.
10. **Review the agent budgets separately**: agent spend should correlate with criticality; high burn on a low-criticality project is a smell (attention residue applies to supervision too).

## (c) Traps

- **Trophy dashboards**: a beautiful board that never triggers a kill/park decision is overhead; the board must make *demoting* projects easy and socially safe.
- **Fake precision**: WSJF/RICE scores on papers are estimates; re-scoring weekly is churn. Score coarsely and infrequently.
- **Budget theater**: hours budgets that are never metered (no Toggl/Timewarrior actuals) become decoration. Budgets only bite if burn is measured passively.
- **Confusing urgency with value**: Eisenhower's classic trap — refereeing and admin always look urgent; the stage-gate view exists to protect slow Stage 0–3 work that produces the papers.
- **Per-project budgets that ignore switching**: the binding constraint is *attention* (Leroy, Mark), not hours; a project within its hours budget can still be too expensive if it's the seventh active project.
- **Agent spend without attribution**: without per-project API keys or proxy tags, agent costs are unattributable and the `agent budget` field will be fiction; wire metering (OpenRouter keys / Helicone properties) before budgeting.
- **Infinite on-hold list**: Burkeman's open list — 60 parked papers with no kill ritual is a guilt archive, not a portfolio. Parked ≠ free.
