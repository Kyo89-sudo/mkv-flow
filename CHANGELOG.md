# Changelog

## 0.4.3 — 2026-10-24 — CRC32 facoltativo

- Opzione globale **Abilita CRC32** nelle Impostazioni, salvata tra gli avvii.
- Se disattivata, batch e rinomina non leggono i file per il CRC e non lo aggiungono al nome.
- Il token `{crc}` e le sue parentesi quadre/tonde vengono ignorati quando il CRC è spento.
- La verifica dei CRC esistenti è nelle Impostazioni ed è disabilitata se il CRC è spento.
- Rimossa l’obbligatorietà del token `{crc}` nel modello batch.
- Fix: rinomina saltata se fallisce la modifica del titolo o la lettura del CRC, evitando valori vecchi.
- Centralizzata la gestione del modello senza CRC; rimosso il parametro inutilizzato di validazione.

## 0.4.2 — 2026-10-24 — Comandi raggruppati

- Navigazione a segmenti affiancati con selezione blu, nello stile del riferimento approvato.
- Comandi raggruppati per funzione, bordi sottili e sfondo chiaro.
- Azioni principali evidenziate; navigazione da tastiera e controlli Win32 conservati.
- Test di rinomina allineato alla modalità GUI: solo MKV, audio e sottotitoli intatti.
- Nessuna modifica al motore di elaborazione dei file.

## 0.4.1 — 2026-10-24 — Rinomina con CRC32

- Struttura `nome [CRC32].mkv` (campo Nome, `{name} [{crc}]`): il vecchio CRC è tolto dal
  nome e riusato senza rileggere il file; il CRC32 è calcolato solo se manca, se cambia
  il titolo interno o con **Verifica i CRC già presenti**.
- Nel batch `{name}` non contiene più il vecchio `[CRC]` del sorgente.

## 0.4.0 — 2026-10-24 — Rinomina episodi

- Nuova scheda **Rinomina**: trova/sostituisci nei nomi di tutti gli MKV di una
  cartella (es. `-IsLAND` → `-MIRCrew`), con anteprima, conferma e annullamento.
- Rinomina sul posto senza sovrascrivere, solo i file MKV; nomi non validi o già
  presenti segnalati e saltati.
- Opzione per aggiornare anche il titolo interno (mkvpropedit) e per usare la stessa
  sostituzione su `{name}` e titolo nel batch.

## 0.3.0 — 2026-10-24 — Editor dei flussi

- Nuova scheda **Flussi**: tabella delle tracce reali (`mkvmerge -J`) con spunta per
  includere/escludere, riordino (▲/▼, Ctrl+↑/↓) e pannello per nome, lingua,
  predefinita, forzata e ritardo. Sostituisce le regole testuali `a1|nome|lingua|…`.
- Tracce esterne audio/sottotitoli/video: pulsante, trascinamento sulla scheda Flussi
  e **Abbina tracce esterne** per nome file; lingua e flag forzato dedotti dal nome.
- **Applica a tutti i file**: modello per tipo e posizione, solo proprietà cambiate,
  differenze di struttura segnalate; applicato anche ai file analizzati dopo.
- **Mantieni lingue** per filtrare audio e sottotitoli in un clic.
- Una sola traccia predefinita per tipo (opzionale).
- Scelta automatica: copia + `mkvpropedit` se cambiano solo le intestazioni,
  remux `mkvmerge` per rimozioni, riordino, esterni e ritardi.
- Opzioni contenitore: rimuovi capitoli, allegati, tag globali.
- Coda con colonne Flussi/Esterni/Stato, barra di avanzamento, analisi prima del batch.
- Finestra 760 × 540, impostazioni aggiuntive salvate in `settings.ini`.
- Test: analisi JSON, layout, modello, argomenti; integrazione con remux, SRT esterno
  e riordino. La workflow installa MKVToolNix ed esegue anche l'integrazione e un
  avvio di prova della GUI; build con avvisi trattati come errori.

## 0.2.0 — 2026-10-23 — Hotfix GUI

- Riprogettata la finestra: area utile iniziale 640 × 460 invece di 930 × 780.
- Coda, Tracce, Impostazioni e Log separati in quattro schede.
- Comandi Avvia/Annulla e stato sempre visibili.
- Analizza apre il Log; i valori dei campi restano disponibili tra le schede.
- Font e spaziature più compatti, layout ridimensionabile con DPI di sistema.
- Numero versione aggiornato nella finestra e nelle proprietà EXE.

## 0.1.0 — 2026-10-23

Prima versione: batch MKV, modifica intestazioni, CRC32 finale e icona 3D.
