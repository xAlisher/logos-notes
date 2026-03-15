# ALISHER.md — My Operating Instructions

## My role
Architect and final decision-maker. Not the relay, not the tester, not the memory.

## What requires my sign-off
- Schema migrations
- Crypto primitive changes
- Major roadmap decisions (new phases, pivots)

## Everything else
Agents handle autonomously. Trust the loop.

## Checking in
```bash
git pull
cat PROJECT_KNOWLEDGE.md   # current state, open findings, open questions
```
That's enough to know what's happening.

## When agents get stuck
Signs: same finding cycling 3+ rounds, disagreement flagged in a GitHub comment.
Action: read the comment, make a call, leave a reply. One sentence is enough.

## Keeping this Claude project in sync
PROJECT_KNOWLEDGE.md lives in the repo AND here in the Claude project context.
After agents update it:
```bash
git pull
```
Re-upload PROJECT_KNOWLEDGE.md here so future sessions stay in sync.
