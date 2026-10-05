---
name: feedback-paid-generation
description: Always warn the user with the cost and get an explicit OK before any paid generation (fal credits etc.)
metadata:
  type: feedback
---

Before any action that spends money (fal.ai generations, any paid API), stop and tell the user what will be generated, which model, and the estimated cost (from get_pricing), then wait for an explicit yes. Applies per batch, every time.

**Why:** 2026-10-05 on the Ape Escape Jake voice work I ran 6 fal TTS jobs right after they topped up, treating the top-up as approval; they asked to be warned and told the cost first from now on.
**How to apply:** free steps (disc extraction, processing, pricing lookups) need no ask; paid runs always do, even cheap ones. Related: [[reference-ape-escape-mod]].
