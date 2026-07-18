# Creator Desk Voice

Status: stored concept, not implemented or hardware-proven.

## Product idea

Creator Desk Voice is a calm, touch-and-voice workspace for an everyday creator
and business owner. It should help run the day, capture ideas, prepare content,
and handle business follow-up without feeling like an analytics dashboard.

The core modes are:

- **Today**: priorities, appointments, errands, and follow-ups.
- **Create**: idea capture, hooks, captions, filming plans, and story prompts.
- **Business**: clients, invoices, deliverables, and items that need a reply.
- **Voice Desk**: push-to-talk capture and spoken guidance with visible
  transcripts and explicit save/action buttons.

Example requests:

- "What are my three priorities today?"
- "Turn this customer question into a post idea."
- "I have twenty minutes. What is the highest-value thing I can do?"
- "Make this sound more confident but still like me."
- "What can I film while I am already at the shop?"

## Proposed architecture

The first useful voice path should use the CrowPanel microphones, touch screen,
and built-in speakers with hold-to-talk. A small trusted gateway owns the API
credential and creator context. The panel must never store an OpenAI API key in
firmware.

```text
CrowPanel mic + touch
        -> secure gateway
        -> realtime or request-based voice service
        -> transcript + explicit actions + spoken response
        -> CrowPanel screen + speakers
```

## Honest proof stages

1. **Audio bench**: microphone meter, short record/playback, speaker level, and
   measured latency on the physical panel.
2. **Push-to-talk assistant**: bounded speech request, transcript, response, and
   audio playback through the gateway.
3. **Realtime Studio Mode**: interruptible conversation, live captions, and a
   narrow set of creator/business tools.

Bluetooth-speaker mode is a later option. The repository does not yet prove the
hosted ESP32-C6 Bluetooth audio path, and the existing CrowPanel audio path is
still hardware-gated. Keep built-in audio, Bluetooth, and OpenAI voice proof as
three separate milestones.

## Relationship to Cypher Desk

The Cypher Desk CrowPanel port can become the local writing and capture surface
for this concept. Voice transcripts, hooks, notes, and checklists can ultimately
save into the same plain-text workspace, while the notebook remains fully useful
offline.
