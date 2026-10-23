# Verifica 0.2.0 — 23 ottobre 2026

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

