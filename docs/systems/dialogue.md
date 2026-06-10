# System: Dialogue & Choices

*Snapshot 2026-06-10. Source: `src/field/field.cpp` (dialogue/choice state), `src/host/host.cpp` (box rendering, message store). Original: `SetMessage`, `GetMessageData`, `ReadStoryMessageData`→`SetMessageList` (`FieldClass+0x380`), `StartMessageSelect`.*

## Text source (HIGH)

Field dialogue is **per-area**: bank N = map *group* (16 groups ↔ 16 `msg{N}.msd` files). The toolkit bakes each bank to `text/msg{N}.bin` (`FMSG`: id → UTF-8 string, English slot). The engine reloads the bank on every map load/warp (`bankOf(key)` parses the `g{N}` prefix; `Host::loadText`). msg-file English text = record × 12 + 2 (6 languages × 2 slots: text + speaker name).

History note: an early cut wrongly used `system_message.msd` §4 as field dialogue (coincidentally coherent); corrected to the real `msg{N}` banks. Don't regress this.

## Flow (HIGH for FFSmith behavior)

- Scripts produce ordered message ids (`VMOut.messages`) → `Field::dlgQueue_`. Confirm advances; queue drain closes the box.
- Movement is frozen while `inDialogue()`; pending warps are held until the queue drains (`consumeWarp` gate).
- **Choices** (`0x3c`): VM pauses; `Field` shows the option list (rendered from each option's *value* interpreted as a message id — **provisional, MEDIUM**: real choice-line source is an open question). Confirm resumes the VM at the chosen block, Cancel at the default (next) block. A choice queued during dialogue is shown when the text drains (`pendingEv_`).
- Speaker names (slot 2 of the msd record) are baked out but **not displayed** — the box shows text only (gap, LOW priority).

## Rendering

Bordered box at the bottom (68 px), word-wrapped `\n`-aware bitmap text (`Host::drawText`, whole-word wrap at `maxChars`); selected choice line highlighted with a `>` cursor. Unknown/multibyte bytes render `?` — **Japanese and accented text is not yet renderable** (font atlas is ASCII 32..126; multi-language support would need a real glyph pipeline).

## Gaps

- No typewriter reveal, no portraits, no message-window positioning (`0x01 ScriptSentence` cinematic placement is flattened into the normal box), no op 0x32 wait.
- Choice cancel-to-default verified only synthetically (`--vmtest`), not against real cutscene data (MEDIUM).
