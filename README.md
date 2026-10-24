<div align="center">
  <img src="assets/icon-3d.png" width="180" alt="Icona MKV Flow">
  <h1>MKV Flow</h1>
  <p><strong>Organizza i tuoi MKV. Modifica i flussi. Rinomina in batch.</strong></p>
  <p>Un programma nativo e leggero per Windows, senza ricodifica di audio e video.</p>
  <p>
    <img alt="Versione 0.4.3" src="https://img.shields.io/badge/versione-0.4.3-1976d2?style=flat-square">
    <img alt="Windows 10 e 11 x64" src="https://img.shields.io/badge/Windows-10%20%2F%2011%20x64-0078D4?style=flat-square">
    <img alt="C++17 e Win32" src="https://img.shields.io/badge/C%2B%2B17-Win32-385875?style=flat-square">
    <a href="LICENSE"><img alt="Licenza MIT" src="https://img.shields.io/badge/licenza-MIT-238636?style=flat-square"></a>
  </p>
  <p>
    <a href="https://github.com/Kyo89-sudo/mkv-flow/releases/latest"><strong>Scarica per Windows</strong></a>
    &nbsp; · &nbsp;
    <a href="#inizia-in-pochi-passaggi">Guida rapida</a>
    &nbsp; · &nbsp;
    <a href="CHANGELOG.md">Novità</a>
    &nbsp; · &nbsp;
    <a href="https://github.com/Kyo89-sudo/mkv-flow/issues">Segnala un problema</a>
  </p>
</div>

---

**MKV Flow** raccoglie in una sola interfaccia la coda dei file, l'editor delle
tracce e la rinomina degli episodi. Puoi lavorare su un singolo MKV o applicare
le stesse modifiche a una serie di file, con anteprima dei nomi e log delle operazioni.

L'app usa **C++17 e Win32** e non richiede Python, Electron, .NET o Qt per funzionare.
Il pacchetto è portabile: estrai lo ZIP e avvia `MKVFlow.exe`.

> **Dipendenza richiesta:** installa separatamente [MKVToolNix per Windows](https://mkvtoolnix.download/downloads.html#windows)
> e seleziona la sua cartella nelle Impostazioni. MKVToolNix non è incluso nel pacchetto.

## Funzioni

| Funzione | Cosa puoi fare | Dove si trova |
| :--- | :--- | :--- |
| **Coda batch** | Aggiungere più MKV o trascinare file e cartelle; seguire stato e avanzamento. | Coda |
| **Editor dei flussi** | Includere, escludere e riordinare le tracce video, audio e sottotitoli. | Flussi |
| **Proprietà delle tracce** | Modificare nome, lingua, flag predefinito/forzato e ritardo. | Flussi |
| **Tracce esterne** | Aggiungere audio e sottotitoli; abbinare automaticamente i file con lo stesso nome. | Flussi / Coda |
| **Modello per la serie** | Applicare ai file del batch le proprietà modificate, per tipo e posizione della traccia. | Flussi |
| **Filtro lingue** | Mantenere audio e sottotitoli nelle lingue desiderate. | Flussi |
| **Titolo e contenitore** | Cambiare il titolo; rimuovere opzionalmente capitoli, allegati e tag globali. | Flussi / Impostazioni |
| **Rinomina episodi** | Trovare e sostituire testo nei nomi, con anteprima e annullamento dell'ultima rinomina. | Rinomina |
| **CRC32 opzionale** | Calcolare il checksum e usarlo nel nome, oppure disabilitarlo per batch e rinomina. | Impostazioni → Checksum |
| **Verifica CRC** | Rileggere gli MKV e confrontare il checksum con quello già presente nel nome. | Impostazioni → Checksum |
| **Copia o remux automatico** | Usare la copia veloce per le sole intestazioni; ripacchettare quando cambia la struttura. | Impostazioni |
| **Protezione dei file** | Conservare gli originali nel batch e impedire la sovrascrittura dei nomi esistenti. | Automatica |
| **Log e annullamento** | Consultare i dettagli e interrompere il lavoro in corso. | Log / Barra inferiore |

## Inizia in pochi passaggi

1. **Scarica** lo ZIP dalla [release più recente](https://github.com/Kyo89-sudo/mkv-flow/releases/latest) ed estrailo.
2. **Configura MKVToolNix** nella scheda Impostazioni. Qui puoi anche attivare o disattivare il CRC32.
3. **Aggiungi gli MKV** alla Coda e scegli la cartella di destinazione.
4. **Modifica i flussi**, se necessario, e usa *Applica a tutti i file* per una serie con struttura compatibile.
5. **Avvia il batch** oppure usa la scheda Rinomina per modificare soltanto i nomi dei file.

## Nomi e checksum, come preferisci

| Impostazione | Modello | Esempio di risultato |
| :--- | :--- | :--- |
| CRC32 attivo | `{name} [{crc}]` | `Episodio 01 [A1B2C3D4].mkv` |
| CRC32 disattivato | `{name} [{crc}]` | `Episodio 01.mkv` |
| Numerazione personalizzata | `Serie - {n}` | `Serie - 1.mkv` |

Il CRC32 si controlla da **Impostazioni → Checksum → Abilita CRC32**.
La preferenza viene salvata. Se è spento, non viene calcolato né aggiunto al nome:
il token `{crc}` e le sue parentesi quadre o tonde vengono ignorati, senza lasciare parentesi vuote.
Il modello scritto resta conservato, pronto per quando riattivi l'opzione.

Nel batch, con CRC attivo, il checksum viene calcolato **dopo tutte le modifiche**.
Nella sola rinomina, un CRC già presente nel nome può essere riusato perché il
contenuto non cambia; puoi richiederne la verifica completa dalle Impostazioni.
Con CRC attivo il valore viene calcolato e scritto nel Log anche se il modello non contiene `{crc}`.

I vecchi tag CRC vengono tolti dal nome di base. Gli altri caratteri del modello
restano invariati: controlla sempre l'anteprima della rinomina.

## Lavora sui flussi senza ricodificare

La tabella della scheda **Flussi** mostra le tracce reali lette con `mkvmerge -J`.
La spunta decide quali mantenere; **▲ Su / ▼ Giù** o **Ctrl+↑/↓** cambiano l'ordine.
Nome, lingua, flag e ritardo si modificano nel pannello della traccia selezionata.

Puoi aggiungere tracce esterne (`.ac3`, `.eac3`, `.dts`, `.aac`, `.flac`, `.opus`,
`.srt`, `.ass`, `.sup` e altri formati supportati). La lingua viene dedotta dal nome,
per esempio `Episodio 01.ita.srt`; `forced` nel nome suggerisce il flag forzato.
**Abbina tracce esterne** associa i file ai rispettivi episodi nella stessa cartella.

**Applica a tutti i file** usa tipo e posizione della traccia e trasferisce solo
le proprietà modificate. Le differenze di struttura vengono segnalate; le tracce
esterne vanno abbinate separatamente. **Mantieni lingue** filtra audio e sottotitoli
con codici come `ita, jpn`.

Se cambiano solo le intestazioni, MKV Flow può copiare il file e modificarlo con
`mkvpropedit`. Riordino, rimozioni, ritardi e tracce esterne richiedono un remux
con `mkvmerge`, senza ricodificare audio o video.

## Rinomina con anteprima

Scegli una cartella, compila **Trova**, **Sostituisci con** e il modello **Nome**.
La tabella mostra *nome attuale → nuovo nome* prima di applicare le modifiche.
Si rinominano **solo gli MKV**: audio e sottotitoli affiancati restano intatti.
Un nome già esistente viene segnalato e saltato.

**Annulla ultima rinomina** ripristina i nomi precedenti. L'opzione
**Aggiorna anche il titolo interno**, invece, modifica gli MKV originali con
`mkvpropedit`: questa modifica del contenuto non viene annullata ripristinando i nomi.
Se fallisce la modifica del titolo o la lettura CRC, quel file viene saltato.

---

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
nella cartella output, scritto, letto per il CRC se abilitato e rinominato nella stessa cartella
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
