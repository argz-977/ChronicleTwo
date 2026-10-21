# Memory safety

This is an audit of `ps2/src` for manual heap ownership and for fixed-size buffers that script, disc, save or player data can overrun. It also lists the native replacements in `port/src` and the sites that are still open.

`ps2/src` has no `malloc`, `free` or `delete[]`. Every `new` except one is placement into an `mgCMemory` arena, which `port/src/mg_memory.cpp` already sizes for the host (see "Arenas" in [PC.md](../PC.md)). The risks that remain are fixed arrays indexed by script values and fixed text fields filled by unbounded copies.

On the PS2, text-script string arguments were capped at 255 bytes by `GetArg`'s token buffer. The port's `GetArg`, `RunNativeConfig` and JSON scripts pass strings of any length. Datapacks can also replace any script. So every sink below takes unbounded input on PC, even where the retail data fits.

## Shared pieces

- `port/src/bounded_text.hpp`: `View(text, capacity)` reads a field up to its terminator or its end. `Copy(field, text)` writes a field, always terminated, cutting before a split two-byte Shift-JIS character. `Fit` and `Fits` measure text against a field.
- `port/src/script_guards.{hpp,cpp}`: table-driven argument rules for script handlers, keyed by the handler's function pointer:
  - `Index` and `IndexOrSelf` (an integer in a range, or -1 for "the current object");
  - `Text` and `TextIfString` (a string length in a range);
  - `MaxArguments` (cap the count a handler sees).

  The rules are applied at two points, which the port already owned:
  - `CRunScript::ext` (`port/src/runscript.cpp`) refuses an external whose arguments break a rule, as it does an illegal call;
  - `CScriptInterpreter::GetNextTAG` (`port/src/scriptinterpreter.cpp`) skips a tag handler, or lowers its argument count.

  Each refused handler is reported once on stderr as `script guard: ...`. Bounds come from the arrays themselves (`std::extent_v<decltype(...)>`), not from copied constants.
- Retail statics are reached through asm link names in `port/include/stubs/<unit>.hpp`. The port builds with `-fms-extensions`, so the unit's later `static` definition keeps the external link name. The new stubs are `effect.hpp`, `dngfloor.hpp` and `dng_event.hpp`. Additions to `inventmn.hpp` and `gamedata.hpp` expose the tables their replacements need.

## Replaced

| Site | Retail problem | Native replacement |
|---|---|---|
| `CGameDataUsed::GetName` (`port/src/userdata.cpp`) | In a non-Japanese language, `strcat(word, " + %d"); sprintf(word, word, level)` uses the player's weapon name as the format. A `%` in a renamed weapon ran arbitrary conversions, and long results overran `word[0x61]` | Built in a `std::string` with `std::format(" + {}", level)`, then copied bounded into the static result |
| `CGameDataUsed::Boiled` | A localised prefix plus the fish name (about 31 bytes) went into `boiled.name[0x16]` | Bounded copy into the field |
| `CGameDataUsed::SetName` | Every family accepted names under 32 bytes, overrunning `fish.name[0x15]` and `attach.name[0x1A]` | Names over 31 bytes are still refused; shorter ones are cut to the family's field |
| `GetPhotoNameCheck` (`port/src/inventmn_text.cpp`) | An icon plus a Spanish idea name (up to 38 bytes) went into `temp_1728[0x21]` on every photo | Result held in a 128-byte static, the size of `PhotoTitle` |
| `MenuInventNetaMemoDraw` | `sprintf(text[0x20], "%s%s", icon, name)` per memo row | Rows built in a `std::string`, copied into 128 bytes (`CFont::SetStr`'s limit) |
| `CSphidaData::EnterScore` (`port/src/savedata.cpp`) | A 20-character name and its terminator into `name[0x14]` | Bounded copy |
| `CScriptInterpreter::GetArgBin` | An `s16` count from the script indexed `types[0x100]`; values and strings were read with no check against the script's size; the returned count could exceed the pushed arguments | `BinaryReader` over `std::span<const char>`; types in a `std::vector`; returns the arguments actually pushed |
| `CScriptInterpreter::SearchCommand` | A text tag name went into `name[0x100]` unbounded; reads one byte past the script | Name in a `std::string`; stops at the script's end |
| `GetNextTAG` dispatch | A handler saw `GetArg`'s count even when the 64-slot stack dropped arguments | Count capped at `stack_count` |
| `CEffectManager::GetBufferNums`, `Load` (`port/src/effect.cpp`) | The only manual heap owner: `__EFFECT_START` did `new CEffectCtrl` and `__EFFECT_END` deleted it. A block with no END leaked, and a second START leaked the first. Setters outside a block wrote through null. `__WAIT_FRAME` indexed `wait_frame[8]` unchecked; `BUFFER_SIZE` allowed more than 8 emitters, overrunning `wait_frame` and `ctrl_name`; `__TEX_GET_RECT` wrote past `tex_rect[8][4]`; `__IMG_NAME` and `__EFFECT_START` used unbounded `strcpy` | A native tag table copied from `effm_tag` with these six handlers replaced. The open emitter is a `std::unique_ptr<CEffectCtrl>`, released at END and at the end of the script. Outside a block, `g_tmp_effc` points at a discard emitter. Emitters are capped at 8, indices and rectangle counts are clamped, and names are copied bounded |
| `_GROUP`, `_ITEM`, `_FLOOR` (`port/src/dng_event.cpp`) | Treasure-table cursors ran past `group[64]`, `item[96]`, `floor[128]` and `group_id[64]` | Cursors checked against each array; overflow entries are dropped |
| `_DATACOM`, `_DATAWEP_BUILDUP` (`port/src/gamedata_tables.cpp`) | The common-item cursor ran past `local_com_itemdata[432]`; `item_no` indexed `local_itemdatano_converttable[0x200]` unchecked; `file_name[16]` took an unbounded copy; the weapon cursor ran past `local_weapondata[116]` | Records past the table are dropped; out-of-range numbers are not indexed; bounded copy. The weapon cursor becomes null at the table's end, which the other weapon tags already skip |
| `MotionProc2` (`port/src/gameutil_asm.cpp`) | Mesh key vertex numbers indexed the global `def_vrtx[800][4]` unchecked | A `std::vector` of 16-byte-aligned accumulators, sized to the skin's vertices; out-of-range keys are skipped |
| `CEffectList::LoadEFPFile` (`port/src/effectlist.cpp`) | `DivPathNameExt` split a pack entry name of up to 63 bytes into 32-byte buffers, then `strcpy`'d it into `CEffectManager::name[32]` | `std::string_view` stem, copied bounded |
| `GetCurrentDir` (`port/src/dataread.cpp`) | Copied a `std::string` of any length into the caller's buffer, which can be as small as 64 bytes (`MenuMonsterLoadBG`) | Bounded to 64 bytes |
| `PreMesMake` (`port/src/nd_meswin_text.cpp`) | Stopped after 512 bytes without a terminator, which `NeedMesWinWH`'s `strcpy` ran past; the `@` branch wrote one byte beyond the buffer | Held to 510 bytes, always double-terminated |
| `ConvertFontCode` (`port/src/menucommon.cpp`) | Looped forever on a `[` whose sixth byte was not `0` or `1`; read and skipped nine bytes past a short string | Only complete nine-byte codes are replaced; other brackets are copied |
| `CAquaMes::SetQuestionId` (`port/src/menuaqua_text.cpp`) | Food names were used as the `sprintf` format; lines went into the 50-byte message name unbounded; read the unwritten tenth food number | `std::format` lines copied bounded; the list is zero-initialised and capped at `MES_NAME_MAX` |

## Guarded

The run-script externals:

| Handlers | Rule |
|---|---|
| `_GET_ACTIVE_MONS_LIFEI`/`LIFEF`, `_SET_ACTIVE_MONS_LIFEI`/`LIFEF`, `_GET_ACTIVE_MONS_MAX_LIFE`, `_SET_DAMAGE_SCORE`, `_GET_MONS_GRADE`, `_SET_ESCAPE_RATE`, `_SET_GUARD_RATE`, `_SET_EXT_PARAM_RATE`, `_GET_ENTRY_OBJ_POS` | argument 0 is -1 or a monster slot in [24, 48) |
| `_GET_ACTIVE_MONS_POS`/`ROT`/`DIST`/`ANGLE`, `_MONS_SE_PLAY`/`STOP`/`LOOP` | argument 0 is a monster slot in [24, 48) |
| `_GET_VALUE`, `_SET_VALUE` | argument 0 indexes `_EFF_SCRIPT::value` |
| `_CHR_SET_POS2`/`ROT2`/`SCALE2`/`MOTION2`/`SHOW2`, `_CHR_ADD_POS2`/`ROT2`/`SCALE2` | argument 0 indexes `_EFF_SCRIPT::sub_chara` |
| `_GOTO_INTERIOR`, `_GOTO_OUTSIDE`, `_MOVE_INTERIOR` | argument 1 is a string shorter than `jump_map_name` |
| `_MAP_JUMP` | argument 1, when a string, is shorter than `jump_map_name` |
| `_LOAD_SCRIPT` | argument 0 is 4 to 61 bytes, which leaves room for the `_1.stb` rewrite in `script_name[0x40]` |
| `_SET_MES_STR` | argument 1 is in [1, `MES_NAME_MAX`]; argument 2, when a string, is shorter than `MES_NAME_LEN` |
| `_GET_EVENT_INFO_SNDID` | argument 0 indexes `snd_id` |
| `_RANDOM_CIRCLE_OFF` | argument 0 is -1 or indexes `CRandomCircle::active` |
| `_HIT_EFFECT` | argument 0 indexes `HitEffect` |
| `_SWE_INIT` | argument 1 indexes `CCharacter2::sword_effect` |

The event externals are `static` in `event_func.cpp`. `SetEventScript` registers them through their `EVENT_EXT_*` slots in the port's event table.

The configuration tags:

| Handler | Rule |
|---|---|
| `_ROOM_LINK` (`RI_LINK`) | at most `GLID_DIR_NUM` arguments |
| `_MENU_ETCINFO2` | argument 0 is a string; at most 1 + 4 arguments |

## Open

These were found by the audit and are not yet replaced. Each needs either a guard rule or a native override; the static handlers need a stub link name.

| Site | Problem |
|---|---|
| `editmenu.cpp` `MakeDownLoadAnaunce` | Word-wrap copy of a condition string of up to 127 bytes into `stack->Alloc(4)` (64 bytes), for non-Japanese strings over 63 bytes |
| `event.cpp` `EventLoop` | Off-by-one terminator on `map_name[0x20]`; `strcat` of `script_name` onto `directory[0x40]` |
| `event_func.cpp` `_FUNCTION_MAP_JUMP`, `_SET_MOVIE_CC`, `_SET_PAKU_ANIM`, `_SET_PAKU_MOTION`, `_SET_MES_ETC` (`EVENT_SET_MES_LINE_ALPHA`), `_SET_GYORACE_ETC`, `_GET_FISHINGTOURNAMENT_ETC` | Unbounded string copies into fixed fields, or unchecked script indices (static) |
| `event_func.cpp` `CheckLoadedBGFile` | Current directory plus a script name into `path[0x80]` |
| `quest.cpp` `quest_NEW`, `quest_COMMENT`; `npccfg.cpp` `_NPC_INFO`; `mapsky.cpp` `_SKY_*`; `menuaqua.cpp` `_GYORACE_LISTNUM` | Script strings into fixed fields; unchecked indices (all static) |
| `actscript.cpp` `_CHECK_EQUIP`, `_SET_ACCUME_FX`, `_SET_SHOT`, `_GET_ATTK_POINT` | Unchecked script indices into `equip[5]`, `object[]` and `slots[3]` |
| `menucommon.cpp` `_MENU_EXE_*`, `_MENU_PART_ETCINFO`, `_MENU_TEXNAME` | Unchecked script indices into `MenuDCMsg[9]`, `etc_info[4]` and `tex_block[16]` |
| `menuop.cpp:1765` (`MenuOptionInit`), `menuchr.cpp:5010` (`MenuMonsterBoxDraw`) | `sprintf(buf, buf, ...)` with overlapping source and destination; both formats are fixed literals |
| `menuchr.cpp` `MenuMonsterLoadBG`, `menuchr.cpp:1122`, `monster.cpp:1108` | `strcat` onto `path[0x40]`; table counts looped over `npc_cmd_mes[3]` and `sword_effect[]` |
| `gameutil.cpp` `MotionProc3`, `AnimeDataInit` | `def_vrtx`/`def_nml[1]` written to the mesh's vertex and normal counts (reached only from `DeformMesh(..., true)`, which no caller passes); `FrameLinkRecord::link[11]` filled once per referencing face |
| `CGameData::GetGuardData` | Checked against the file's `guard_num`, not `local_guarddata[35]` |
| `userdata.cpp` `GetMsgAddInfo`; `nameregi.cpp` `KeyStep`; `charasetup.cpp` `GetCharacterSnd`; `vlgr_info.cpp` `GetVillagerModelName` | Overflow only with crafted save or configuration data |
| `effscript.cpp`, `sceneseq.cpp`, `eventsprite.cpp`, `character.cpp` chr-script tags, `menucls1.cpp` `CDC2Mes::SetMsgItemNo` | Script strings into 0x20–0x40-byte name fields |
| Debug and editor units (`mapselect`, `mainloop3`, `movieviewlp`, `editdebug`, `edit*`) | Same patterns; not reachable in ordinary play |

## Tests

`port/src/tests/memory_safety_test.cpp`:

- `BoundedText.*`: Shift-JIS-safe truncation and field views.
- `BinaryScript.*`: a 300-argument tag sees 64 arguments; a tag whose values run off the script sees only the complete ones.
- `ItemNames.*`: a renamed weapon named `%s%n%99999d` prints literally; fish and boiled names stay inside their fields.
- `ScriptGuards.*`: `_GET_VALUE` is refused outside `_EFF_SCRIPT::value`.
