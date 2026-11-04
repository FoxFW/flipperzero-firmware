# Notizen NFC APDU Spy (für spätere Chats)

- Basis: Badge Spy v1.0 (gleiche NFC-Logik, ISO-DEP von Hand, answers.txt).
- v1.1: umbenannt (appid `nfc_apdu_spy`, Ordner `apps_data/nfc_apdu_spy`), Graustufen-Oberfläche.
- v1.2: Standard = 1-Bit-Pixel-Art (Bilder vom Nutzer), Testmenü (OK lang) mit MODE 1-BIT/GRAY, S3..S8, Rate.
  Settings-Datei neues Format `v2 gray=0 s=4 rate=60`.
- Bildschirme: Wait (ov_get_near_target) → Hold (ov_hold_still, 1,5 s) → Log (ov_frame + Text)
  → Save (Rahmen + ov_check, 1 s) → Done (ov_data_saved, 2 s) → Ende.
- Grafik: `spy_art.h` generiert aus `art_src/` (Quellen 128×65, oberste Zeile wird weggelassen).
  `spy_gfx.h`: malt in `lev[64][128]` (0..S), `gfx_render_subframe()` → 1-Bit-Bild.
- Timing: Graustufen = S Teilbilder (DWT-Zähler); 1-Bit = 1 Bild pro Takt (RATE/S Bilder pro Sekunde).
- v1.3: umbenannt „NFC Apdu Spy“. Hauptschirm: L/R=Grafik, OK=Menü, Down=About, Back=Stop→Save?-Abfrage.
  Settings-Menü im Flipper-Look (menu_page/menu_row in der .c). Tag-Presets (tag_presets[], Typ A): Badge/Generic/Random,
  je eigene Answer-Datei (answers_badge.txt / answers_generic.txt). ATS pro Preset in app->ats.
  Log: erst spy_tmp.txt, bei YES rename auf spy_JJJJ-MM-TT_HHMMSS.txt. Settings-Format v3 gray/s/rate/tag.
  Vibration (sequence_single_vibro) + HOLD-STILL blinkt beim ersten Kontakt.
- v1.0.0 (öffentliche Version): Settings-Menü mit Back-Button unten rechts (zeigt Ziel), änderbare Werte in < >,
  Tag-Namen als ISO-Bezeichnung (ISO14443-4A / min / Random UID 4A), aktiver Tag mit Punkt-Marker.
  Separator bei Zeile 51 (Bildunterkante), Buttons wie Stop (Reihen 53-63). Save-Abfrage: < NO / YES >, SAVE LOG?.
  Nach YES sofort Data-SAVED-Screen mit Dateiname mittig (kein Stop/Haken), ~3,5 s, dann Ende.
  ov_check bleibt in der Grafik, wird aber nicht mehr genutzt.
- v1.0.0: Tag-Profile sind jetzt Dateien in profiles/ (Ordner-Scan). Format: Kopf UID/ATQA/SAK/ATS/RANDOM_UID (optional)
  + Antwortzeilen; erste #-Zeile = Menuname. spy_parse_profile() parst Identitaet+Antworten. Settings v4: tag=<dateiname>.
  Select-Liste scrollt (tag_top). TODO naechster Schritt: Startmenue (Text: SELECT TARGET/SCAN TAG) + Scan-Tag (GFX: Mission SCAN TAG + X).
- v1.0.0 Ablauf NEU: App startet im Startmenu (MenuHome): Text SELECT TARGET / SCAN TAG + ">"-Marker ueber Delphin-BG.
  SELECT TARGET -> Profilliste -> OK startet Spy. SCAN TAG -> ov_scan_tag + Reader (iso14443_3a_poller_sync_read, 250ms),
  Erfolg -> Profil scan_<UID>.txt + ov_check 1s -> Spy; Fehler -> gfx_cross (X, Platzhalter) 1s -> Home. Back im Home = App zu.
  Listener laeuft nur im Spy (spy_listener_start/stop_now). spy_enter() startet Session, spy_goto_home() zurueck.
  Spy Back -> SAVE LOG? -> danach Home (nicht mehr App-Ende). TODO: On-Screen-Keyboard zum Umbenennen nach Scan; echtes X-GFX.
- v1.0.0 GFX-Paket vom Nutzer eingebaut: Home = ov_home_scan/ov_home_spy (SCAN TAG/SPYING, 2 Zustaende).
  Headlines: ov_get_near_target=SPY ON TARGET, ov_scan_tag=SCAN TAG NOW, ov_hold_still, ov_data_saved, ov_check, ov_fail(=Verbotsschild).
  Buttons als Sprites (btn_back/no/yes/exit/stop) via gfx_button(). Home sel0=SCAN TAG->Scan, sel1=SPYING->Tag-Liste->Spy.
