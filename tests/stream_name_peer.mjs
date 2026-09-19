// SPDX-License-Identifier: GPL-3.0-or-later
// The stream a message is filed under for `resume`, with the browser's own code, driven by
// tests/test_remote_wire.py. The names must be the hub's (remote/host.py), or a resume names
// streams the hub does not keep and is silently ignored.
import { Rrp } from '../app/rrp.js';

const messages = JSON.parse(process.argv[2]);
process.stdout.write(JSON.stringify(messages.map((message) => Rrp.streamOf(message))));
