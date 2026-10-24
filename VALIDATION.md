# Verifica 0.4.2 — 24 ottobre 2026

- Build Release x64 con Visual Studio 2026 / MSVC v145 e avvisi trattati come errori: riuscita.
- Suite core (91 verifiche), processi e integrazione reale con MKVToolNix v103.0: superata.
- Rinomina sul posto verificata in una cartella temporanea del workspace: solo MKV;
  sottotitoli/audio affiancati intatti, collisioni senza sovrascrittura.
- Aspetto della Coda controllato sull'anteprima approvata: selezione blu e comandi uniti.
- Nessun cambiamento al motore; la personalizzazione riguarda il disegno dei pulsanti Win32.
- Non verificati DPI multipli, contrasto elevato o tutte le combinazioni di focus/tema.
- Pacchetto non firmato digitalmente.

# Verifica 0.4.0 / 0.4.1 — 24 ottobre 2026

- Test core per trova/sostituisci, piano di rinomina (file affiancati, collisioni,
  nomi non validi, maiuscole), sostituzione nei nomi e titoli del batch; eseguiti
  su Windows in CI e su Linux con stub Win32 e ASan/UBSan.
- Test per `[CRC]` nei nomi: riconoscimento, rimozione, riuso senza rilettura,
  file affiancati con e senza CRC, nomi già corretti saltati.
- Test Windows della rinomina reale: nomi applicati e mai sovrascritti.
- Non verificati a mano: scheda Rinomina nella GUI e aggiornamento del titolo interno.

# Verifica 0.3.0 — 24 ottobre 2026

- GitHub Actions (windows-latest, MSVC v143, `/warnaserror`): build Release x64 riuscita.
- Suite core superata: analisi JSON di `mkvmerge -J` (escape Unicode e surrogati,
  file non riconosciuti, JSON troncato), layout predefinito, predefinita unica per tipo,
  argomenti `mkvpropedit` per sole intestazioni, argomenti `mkvmerge` per riordino,
  rimozione, ritardo, opzioni contenitore e tracce esterne, modello batch con proprietà
  non modificate conservate e differenze segnalate, filtro lingue, abbinamento file.
  La stessa suite è stata eseguita anche su Linux con stub Win32 e sanitizer ASan/UBSan.
- Test processi superati (argomenti Unicode, output UTF-8, exit code, annullamento).
- Integrazione con MKVToolNix installato da Chocolatey superata: fixture sintetica con
  due audio e SRT esterno; copia veloce con titolo, nomi e flag verificati con `-J`;
  remux con riordino, rimozione, SRT esterno (lingua dal nome, nome, ritardo); CRC nel
  nome, originali intatti, collisione senza sostituzione, errore mkvmerge con pulizia
  del temporaneo, annullamento.
- Avvio di prova della GUI in CI: finestra creata e chiusa correttamente.
- Non verificati: aspetto visivo della nuova scheda Flussi, uso interattivo completo
  della GUI, monitor con DPI diversi, file video reali di grandi dimensioni.
- Eseguibile non firmato digitalmente.

## Verifiche 0.2.0

- Build Release x64 con Visual Studio 2026 riuscita.
- Suite core e wrapper processi superata.
- Verifica visiva della nuova finestra compatta e delle quattro schede.
- Motore MKV invariato rispetto alla release precedente; integrazione MKVToolNix
  non ripetuta per questa modifica della sola interfaccia.
- La compilazione/test GitHub Actions della 0.1.0 era passata; la 0.2.0 viene
  verificata anche dal workflow al caricamento dei sorgenti.
- Non verificati monitor con DPI diversi, né elaborazione GUI end-to-end su video grandi.
- Eseguibile non firmato digitalmente.

## Verifiche della versione precedente


- Compilazione Release x64 riuscita con Visual Studio 2026, MSVC v145 / 14.51.
- Test automatici core superati: vettore CRC32 standard e streaming, quoting,
  validazione nomi, espansione token non ricorsiva, parsing regole.
- Test wrapper processi superati con processo figlio di prova: argomenti Unicode,
  virgolette e backslash, output UTF-8 anche senza newline finale, exit code 1,
  annullamento di un processo in esecuzione.
- Prima esecuzione integrazione con MKVToolNix v102.0 superata: fixture MKV sintetica,
  remux e copia/modifica, CRC nel nome uguale al file scritto, originali intatti,
  collisione senza sostituzione, selettore assente e pulizia temporanei.
- MKVToolNix non era più disponibile per la verifica successiva. Le ultime modifiche
  (gestione dell'ultima riga di output, diagnostica avvio e test espliciti sulle
  proprietà JSON) non hanno ricevuto una nuova esecuzione di integrazione reale.
  I test core/processi sono stati ripetuti dopo le modifiche. Per ripetere la suite
  completa: `test.cmd "percorso\MKVToolNix"`.
- GUI avviata e ispezionata, aggiunta regola verificata. Due etichette accorciate
  dopo l'ispezione. Nessuna verifica automatica completa end-to-end della GUI,
  né prova su monitor con DPI differenti o file MKV video di grandi dimensioni.
- La workflow GitHub della 0.1.0 è stata eseguita con successo.
- Il programma e i pacchetti non sono firmati digitalmente.

