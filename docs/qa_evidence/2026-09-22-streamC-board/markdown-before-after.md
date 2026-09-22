<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Card #MDX6 — the same card bodies, before and after

Written by `render_drive.py`: one headless Chrome, both renderers imported from the
same origin, the same strings drawn through each. `before` is `458582b2:app/boardmd.js`.

| card body | what it is | before | after |
|---|---|---|---|
| ```` - run this:\n\tmake all ```` | a command under a bullet, indented with a tab | 'run this:' / 'ake all'; 1 list(s), 0 nested | 'run this:' / 'make all'; 1 list(s), 0 nested |
| ```` - a\n\t- b ```` | a nested bullet, indented with a tab | 'a' / 'b'; 1 list(s), 0 nested | 'a' / 'b'; 2 list(s), 1 nested |
| ```` - item\n    ```\n\tb\n    ``` ```` | a tab-indented line of a fence in a list item | 'item'; 1 list(s), 0 nested; fence '' | 'item' / 'b'; 1 list(s), 0 nested; fence 'b' |
| ````     ```sh\n\tmake all\n    ``` ```` | a tab-indented line of an indented fence | 'e all'; fence 'e all' | 'make all'; fence 'make all' |
| ```` the __init__ method and a__b__c ```` | a Python dunder in a sentence | 'the init method and abc'; bold 'init' / 'b' | 'the __init__ method and a__b__c' |
| ```` Results:\n\| a \| b \|\n\| --- \| --- \|\n\| 1 \| 2 \| ```` | a table written straight under a sentence | 'Results:' / '\| a \| b \|' / '\| --- \| --- \|' / '\| 1 \| 2 \|' | 'Results:' / 'a' / 'b' / '1' / '2'; 1 table |
| ```` __also bold__ and **bold** ```` | bold with underscores, which is not a name | 'also bold and bold'; bold 'also bold' / 'bold' | 'also bold and bold'; bold 'also bold' / 'bold' |

6 of 7 bodies drew differently.
