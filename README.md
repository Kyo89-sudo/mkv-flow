# MKV Flow

<img src="assets/icon-3d.png" width="128" alt="MKV Flow 3D icon">

**Flussi, rinomina, verifica.** Una piccola GUI nativa Windows per modificare i
flussi video, audio e sottotitoli di file MKV in batch, senza ricodifica.
C++17 e Win32; nessun runtime Python, Electron, .NET o Qt. Versione **0.4.1**,
interfaccia in italiano.

## Novità 0.3: editor dei flussi

La vecchia casella di regole testuali (`a1|Italiano|ita|1|0`) è sostituita da una
scheda **Flussi** che mostra le tracce reali del file, lette con `mkvmerge -J`:

| # | Tipo | Codec | Lingua | Nome | Pred. | Forz. | Ritardo | Dettagli | Origine |
|---|------|-------|--------|------|-------|-------|---------|----------|---------|
| ☑ 1 | Video 1 | HEVC | und | | Sì | | | 1920x1080 | MKV · ID 0 |
| ☑ 2 | Audio 2 | E-AC-3 | ita | Italiano | Sì | | | 6 ch, 48 kHz | MKV · ID 2 |
| ☐ 3 | Audio 1 | AAC | jpn | Originale | | | | 2 ch, 48 kHz | MKV · ID 1 |
| ☑ 4 | Sottotitoli | SRT | ita | Cartelli | | Sì | +250 ms | | Ep 01.ita.forced.srt |

- **Spunta** = la traccia resta; togli la spunta per rimuoverla.
- **▲ Su / ▼ Giù** (o Ctrl+↑/↓) cambiano l'ordine finale delle tracce.
- Sotto la lista si modificano **nome, lingua, predefinita, forzata e ritardo** della
  traccia selezionata. Le modifiche sono segnate con ✎.
- **Una sola predefinita per tipo**: attivare la predefinita su un audio la toglie agli
  altri audio (disattivabile in Impostazioni).
- **Tracce esterne**: *Aggiungi traccia esterna…* o trascina file audio/sottotitoli
  sulla scheda Flussi (`.ac3 .eac3 .dts .aac .flac .opus .mka .srt .ass .sup …`).
  La lingua è dedotta dal nome (`Episodio 01.ita.srt` → `ita`), `forced` nel nome
  imposta il flag forzato.
- **Mantieni lingue** (`ita, jpn`) toglie in un clic audio e sottotitoli in altre lingue.
- **Applica a tutti i file** copia il layout del file aperto su tutta la coda: le tracce
  sono riconosciute per tipo e posizione (secondo audio, primo sottotitolo…). Passano
  solo le proprietà modificate: i nomi non toccati restano quelli di ciascun episodio.
  I file con struttura diversa sono segnalati in coda e nel Log.
- **Abbina tracce esterne** (scheda Coda) aggiunge a ogni episodio i file con lo stesso
  nome nella sua cartella: `Ep 01.mkv` + `Ep 01.ita.ac3` + `Ep 01 [eng].srt`.

Se cambiano soltanto nomi, lingue o flag, il file è copiato e modificato con
`mkvpropedit` (veloce). Rimozioni, riordino, tracce esterne e ritardi richiedono un
remux con `mkvmerge`, sempre senza ricodificare audio o video.

## Rinomina episodi (0.4)

La scheda **Rinomina** rinomina in blocco gli MKV di una cartella mantenendo la
struttura `nome [CRC32].mkv`, per esempio da `S01E01-IsLAND [A1B2C3D4].mkv` a
`S01E01-MIRCrew [A1B2C3D4].mkv` per tutta la stagione:

1. Scegliere la cartella (o **Usa output** per quella del batch).
2. Facoltativo: **Trova** `IsLAND`, **Sostituisci con** `MIRCrew`.
3. **Nome** (predefinito `{name} [{crc}]`): `{name}` è il nome attuale senza il vecchio
   `[CRC]`, dopo il trova/sostituisci; `{crc}` il CRC32; `{n}` il numero in ordine alfabetico.
   L'anteprima mostra subito *nome attuale → nuovo nome* e gli eventuali problemi.
4. **Rinomina**, poi confermare. **Annulla ultima rinomina** rimette i nomi di prima.

Rinominare non cambia il contenuto, quindi un CRC già presente nel nome viene **riusato**
senza rileggere il file: la rinomina è immediata. Il CRC32 viene calcolato solo per i file
che non lo hanno nel nome, per quelli di cui si modifica il titolo interno, oppure per
tutti con **Verifica i CRC già presenti**, che segnala nel Log i nomi con CRC sbagliato.

- Lavora sul posto: nessuna copia, nessun remux, i file esistenti non vengono mai
  sovrascritti (un nome già presente è segnalato e quel file viene saltato).
- Sottotitoli e audio con lo stesso nome (`S01E01-IsLAND.ita.srt`) seguono il video.
- Facoltativo: **Aggiorna anche il titolo interno** applica la stessa sostituzione al
  titolo del contenitore con `mkvpropedit` (modifica i file originali; non annullabile).
- Facoltativo: la stessa sostituzione può valere per `{name}` e per il titolo nel batch,
  così l'output esce già con il nuovo gruppo.

## Funzioni

- Coda di file MKV con aggiunta multipla e trascinamento di file/cartelle.
- Editor dei flussi descritto sopra, con modello applicabile a tutto il batch.
- Titolo del contenitore con `{name}` e `{n}`; rimozione opzionale di capitoli,
  allegati e tag globali.
- CRC32 IEEE sul file **dopo** remux e modifiche, inserito nel nome.
- Output separato, originali intatti, nessuna sovrascrittura di file esistenti.
- Barra di avanzamento, stato per file nella coda, annullamento in ogni fase.
- Impostazioni ricordate in `%LOCALAPPDATA%\MKVFlow\settings.ini`.

MKVToolNix è una dipendenza **esterna**, non inclusa né modificata. Installarlo da
[mkvtoolnix.download](https://mkvtoolnix.download/downloads.html#windows), conservando
la sua distribuzione completa: copiare soltanto i due EXE potrebbe non bastare.

## Compilare con Visual Studio 2026

1. Nel Visual Studio Installer installare **Sviluppo di applicazioni desktop con C++**,
   il toolset **MSVC v145 x64/x86** e un Windows SDK 10/11.
2. Aprire `MKVFlow.sln` in Visual Studio 2026.
3. Selezionare **Release / x64**, quindi **Compila soluzione** (`Ctrl+Maiusc+B`).
4. Avviare `bin\Release\MKVFlow.exe`. Non è richiesta installazione; il runtime C++
   della build Release è collegato staticamente. MKVToolNix resta esterno.

Da **x64 Native Tools Command Prompt for VS 2026**, nella cartella del progetto:

```bat
msbuild MKVFlow.sln /m /p:Configuration=Release /p:Platform=x64
test.cmd
test.cmd "C:\Program Files\MKVToolNix"
```

Il primo test verifica analisi JSON, layout dei flussi, modello batch, argomenti di
mkvmerge/mkvpropedit, CRC, quoting, nomi e processi. Il secondo crea un MKV sintetico
con due audio e un SRT esterno, poi verifica copia veloce, remux con riordino,
rimozione e traccia esterna, proprietà effettive, CRC finale, collisioni, originali,
errori e pulizia. Gli artefatti sono in `test-output/` (ignorata da Git).

Il progetto usa C++17 e compila anche con toolset v143:
`msbuild MKVFlow.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143`.
La workflow GitHub usa v143, installa MKVToolNix con Chocolatey ed esegue entrambe
le suite; produce l'EXE come artifact, senza pubblicare release.

## Uso

1. **Impostazioni**: selezionare la cartella con `mkvmerge.exe` e `mkvpropedit.exe`.
2. **Coda**: aggiungere o trascinare MKV/cartelle (solo primo livello), scegliere la
   cartella output e il modello nome **senza estensione**:
   - `{name} [{crc}]` → `Episodio 01 [A1B2C3D4].mkv`
   - `Serie - {n} [{crc}]` → `Serie - 1 [A1B2C3D4].mkv`
3. Facoltativo: **Abbina tracce esterne** per aggiungere audio/sottotitoli con lo stesso nome.
4. Doppio clic su un file → scheda **Flussi** (l'analisi parte da sola). Sistemare
   spunte, ordine e proprietà; poi **Applica a tutti i file** se gli episodi hanno la
   stessa struttura.
5. **Avvia batch**. I file non ancora analizzati vengono analizzati prima; quelli
   analizzati dopo aver applicato il modello lo ricevono automaticamente.

Lingue: `ita`, `eng`, `jpn` o tag BCP 47 supportati da MKVToolNix (`pt-BR`).
Ritardo in millisecondi, anche negativo. `{crc}` è ammesso solo nel nome file:
nel titolo cambierebbe di nuovo il checksum.

## Comportamento e limiti

- Un file alla volta, su thread di lavoro; la finestra resta reattiva.
- Gli avvisi MKVToolNix (exit code 1) sono segnalati e l'output viene conservato.
  Exit code maggiori di 1 fanno fallire il file, il batch prosegue.
- Le tracce esterne sono per file: *Applica a tutti* non le copia (usare *Abbina*).
- Nessuna conversione codec, editor di capitoli/tag o elaborazione parallela.
- Nel batch un vecchio `[CRC]` alla fine del nome sorgente viene tolto da `{name}`.
- Durante il lavoro serve spazio per un file output intero.
- L'annullamento mantiene gli output completati ed elimina quello in corso. Dopo un
  arresto forzato può restare un `mkf*.tmp` nella cartella output: rimuoverlo a mano.
- Percorsi Unicode supportati; percorsi molto lunghi non garantiti (`GetTempFileNameW`).
- Windows 10/11 x64, DPI di sistema; nessun adattamento tra monitor con scale diverse.
- Le modifiche ai flussi valgono per la sessione: non sono salvate su disco.

## Struttura e sicurezza dei file

`src/core.*`: analisi JSON, modello dei flussi, modello batch, argomenti MKVToolNix,
processi, pipeline, CRC e nomi. `src/main.cpp`: finestra, coda, editor e worker.
`src/app.rc`: icona/versione. `tests/tests.cpp`: test senza framework.

Gli argomenti sono quotati con le regole Windows e passati direttamente a
`CreateProcessW`, senza shell. L'ereditarietà degli handle è limitata; un Job Object
termina il processo esterno e i figli all'annullamento. Il file temporaneo è creato
nella cartella output, scritto, letto per il CRC e rinominato nella stessa cartella
senza il flag di sostituzione: una collisione non sovrascrive l'esistente.
Nessun servizio di rete o telemetria nell'app.

## GitHub e licenza

Sorgenti e icona originali distribuiti con licenza MIT (`LICENSE`). Nessun codice
copiato dal progetto [MKV MultiMux GUI](https://github.com/MrTOgRaS/MKV-Multi-Mux-GUI),
consultato soltanto come riferimento funzionale. MKVToolNix ha una licenza propria.
Repository: https://github.com/Kyo89-sudo/mkv-flow. Nessun EXE MKVToolNix va incluso nel Git.

Fonti tecniche: [mkvmerge](https://mkvtoolnix.download/doc/mkvmerge.html),
[mkvpropedit](https://mkvtoolnix.download/doc/mkvpropedit.html).
Icona: `python tools/package_icon.py` (Python + Pillow), provenienza in `assets/ICON-PROMPT.md`.
La versione è definita in `src/version.h`.
