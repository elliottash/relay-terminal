---
name: domain-purchase
description: Register a domain name through the registrar API with a price ceiling and the person's click before anything is charged. "buy the domain", "register example.org".
short: 'Register a domain through the registrar API; a price ceiling and one click before money moves.'
profile: |
  artifact: system
  primary: probe
  also: script
  human: required
  criteria: the exact name, the registrar, the total price and the auto-renew setting match the request before the click
  sign_off: money
  effort: low
  stakes: money
  blast: case
  regularity: routine
  executable: yes
  rot: medium
  rot_reason: registrar API, prices and TLD rules change
  confidential: no
  money: yes
---

# Domain purchase

Money moves in step 5 and nowhere else; every step before it is read-only.

1. Normalise the requested name (lower case, one TLD) and check it is not already in the
   account's domain list.
2. Query availability and the first-year and renewal prices through the registrar API.
3. Refuse and stop if the total is above the ceiling the person set, or if the TLD needs
   documents the account does not hold.
4. Show the person one line: name, registrar, first-year price, renewal price, auto-renew on
   or off. Wait for their click; nothing is queued behind it.
5. Place the order with the API, keeping the order id and the receipt.
6. Read the domain back through the API (status, expiry, nameservers) until it reports
   registered, or report the failure with the order id.
7. Record the receipt beside the case; a refund, if one is needed, is the person's call.
