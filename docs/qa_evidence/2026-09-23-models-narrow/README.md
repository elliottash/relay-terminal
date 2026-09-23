# Models pane at narrow width

The Qt widget captures use a 420 × 720 pane with a small provider catalog and a reported job model. They cover every tab and the transition back to a wide pane:

| Tab | Capture | What it shows |
| --- | --- | --- |
| Providers | [01-providers.png](01-providers.png) | Search and provider action fit. |
| Available | [02-available.png](02-available.png) | Availability tick and model name fit; no reasoning control. |
| Priorities | [03-priorities.png](03-priorities.png) | Rank, box cutoff and model name fit; no reasoning control. |
| Effort | [04-effort.png](04-effort.png) | Ranked model and its reasoning levels. |
| Jobs | [05-jobs.png](05-jobs.png) | Job and current model fit, with full selected job details and a model button below. |

The isolated Relay run in `drive.sh` used an 840 px window split between a terminal and the Models pane. Its captures show the real Providers [06](06-live-providers.png), Available [08](08-live-available.png), Priorities [09](09-live-priorities.png), Effort [07](07-live-effort.png), and Jobs [10](10-live-jobs.png) tabs. [11](11-live-jobs-popup.png) shows the model picker opening from a job row.

No account or key was configured in the isolated app. The Jobs table therefore shows a dash in “runs on”; the widget test supplies a worker report and verifies that text and the compact controls.
