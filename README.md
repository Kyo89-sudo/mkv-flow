# MKV Flow

<img src="assets/icon-3d.png" width="128" alt="MKV Flow 3D icon">

**Modifica, rinomina, verifica.** Una piccola GUI nativa Windows per elaborare
file MKV in batch, senza ricodifica. C++17 e Win32; nessun runtime Python,
Electron, .NET o Qt. Versione **0.2.0**, interfaccia in italiano.

## Interfaccia compatta (0.2.0)

Finestra iniziale con area utile di 640 × 460 pixel logici, ridimensionabile.
Le quattro schede separano Coda, Tracce, Impostazioni e Log. Le impostazioni
restano conservate passando da una scheda all’altra; Avvia e Annulla sono sempre
accessibili. Il pulsante Analizza apre automaticamente il Log.

## Funzioni

- Coda sequenziale di file MKV con aggiunta multipla e trascinamento di file/cartelle.
- Remux tramite `mkvmerge`, oppure copia e modifica tramite `mkvpropedit`.
- Titolo del contenitore, nome/lingua delle tracce, flag predefinito e forzato.
- CRC32 IEEE sul file **dopo** remux e modifica delle intestazioni, inserito nel nome.
- Output separato, originali intatti, nessuna sovrascrittura di file esistenti.
- Annullamento durante copia, processi esterni e calcolo CRC; pulizia del temporaneo.
- Analisi JSON del file selezionato tramite `mkvmerge -J`, visibile nel log.
- Percorsi e modello di rinomina ricordati in `%LOCALAPPDATA%\MKVFlow\settings.ini`.
- Icona 3D dedicata inclusa in PNG trasparente e ICO multirisoluzione.
- Versione 0.2.0 visibile nella finestra e nelle proprietà dell'eseguibile.

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

Il primo test verifica CRC, quoting Windows, modelli, validazione nomi e regole.
Il secondo genera un piccolo WAV sintetico, lo converte in MKV con mkvmerge e
verifica entrambi i flussi, proprietà effettive, CRC finale, collisioni, originali,
errori e pulizia. Gli artefatti di test sono in `test-output/` (ignorata da Git).
Non servono file multimediali personali. Eseguire i test da una cartella scrivibile.

Il progetto usa C++17 e può essere compilato anche con toolset v143:
`msbuild MKVFlow.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143`.
La workflow GitHub usa questo toolset sui runner Windows; la soluzione locale
seleziona v145 per VS 2026. La workflow produce l'EXE come artifact, senza pubblicare release.

## Uso

1. Nella scheda **Impostazioni**, selezionare la cartella contenente `mkvmerge.exe` e `mkvpropedit.exe`.
2. Aggiungere MKV o trascinare file/cartelle sulla finestra. Le cartelle sono lette
   soltanto al primo livello; i percorsi già presenti non sono aggiunti due volte.
3. Scegliere una cartella output scrivibile. Può essere quella dei sorgenti:
   una collisione del nome finale viene comunque rifiutata.
4. Impostare il modello nome **senza estensione**, per esempio:
   - `{name} [{crc}]` → `Episodio 01 [A1B2C3D4].mkv`
   - `Serie - {n} [{crc}]` → `Serie - 1 [A1B2C3D4].mkv`
5. Nella scheda **Tracce**, per cambiare il titolo spuntare **Imposta titolo**. Accetta `{name}` e `{n}`;
   il campo vuoto imposta un titolo vuoto. `{crc}` è consentito solo nel nome file:
   inserirlo nel contenitore cambierebbe nuovamente il checksum.
6. Aggiungere le regole traccia con i campi, poi **Aggiungi regola**. Le regole
   si applicano a ogni file della coda e possono anche essere modificate nel riquadro.
7. Lasciare **Remux** attivo per ripacchettare; disattivarlo per una copia del file
   seguita dalla sola modifica delle intestazioni. Premere **Avvia batch**.

### Regole traccia

Ogni riga: `selettore|nome|lingua|default|forced`.

```text
a1|Italiano|ita|1|0
a2|Originale|jpn|0|0
s1|Italiano completo|ita|0|0
s2|Cartelli|ita|0|1
```

`a1` = primo audio; `a2` = secondo audio; `s1` = primi sottotitoli;
`v1` = primo video; `1` = prima traccia in ordine generale. Tutti partono da **1**:
non sono gli ID zero-based di mkvmerge. L'analisi mostra ordine, tipo e proprietà
nel JSON. Un selettore assente produce errore per quel file, lasciando proseguire
il batch. Verificare che gli episodi abbiano la stessa struttura prima di applicare
regole comuni. Non sono presenti anteprima tabellare o mapping automatico delle tracce.

Campo vuoto = conserva la proprietà; nome `<vuoto>` = rimuove il nome traccia.
Lingue: `ita`, `eng`, `jpn` o tag BCP 47 supportati da MKVToolNix. Flag: vuoto,
`0` o `1`. Non si azzerano automaticamente le altre tracce quando una è impostata
come predefinita: aggiungere esplicitamente le regole per le altre tracce.
Il carattere `|` non è ammesso nei campi delle regole.

## Comportamento e limiti della 0.2

- Un file alla volta, su thread di lavoro; GUI disponibile durante il batch.
- Log dello strumento e riepilogo; gli avvisi MKVToolNix (exit code 1) sono segnalati
  e l'output viene conservato. Exit code maggiori di 1 fanno fallire il file.
- CRC32 sempre calcolato, in streaming con buffer da 1 MiB. Il nome deve contenere
  `{crc}`. È un controllo di integrità, non un hash crittografico.
- Nessun filtro/rimozione/riordino tracce, conversione codec, mux da file separati,
  capitoli/tag editor o elaborazione parallela in questa versione.
- I vecchi CRC nel nome sorgente non vengono rimossi automaticamente.
- Durante il lavoro serve spazio per un file output intero; il remux può cambiare
  dimensioni e metadati del contenitore senza ricodificare audio/video.
- L'annullamento mantiene gli output già completati. Quello in corso viene eliminato.
  Un arresto forzato o blackout può lasciare un `mkf*.tmp` nella cartella output:
  rimuoverlo manualmente dopo aver chiuso l'app. Non esiste ripresa automatica.
- Percorsi Unicode supportati; i percorsi molto lunghi non sono garantiti:
  creazione temporanei limitata da `GetTempFileNameW`/MAX_PATH. Usare cartelle brevi.
- Windows 10/11 x64. DPI di sistema, tema Windows e navigazione Tab nativi;
  non c'è adattamento dinamico tra monitor con scale diverse.
- Impostazioni di lavoro (titolo e regole) non persistite; solo percorsi e modello nome.

## Struttura e sicurezza dei file

`src/main.cpp`: finestra, coda e worker; `src/core.*`: regole, processi, pipeline,
CRC e nomi. `src/app.rc`: icona/versione. `tests/tests.cpp`: test senza framework.

Gli argomenti sono quotati con le regole Windows e passati direttamente a
`CreateProcessW`, senza shell. L'ereditarietà degli handle è limitata; un Job Object
termina il processo esterno e i figli all'annullamento. Il file temporaneo è creato
nella cartella output, modificato, letto per il CRC e rinominato nella stessa cartella
senza il flag di sostituzione: una collisione non sovrascrive l'esistente.
Non inserire l'output in cartelle condivise con processi che ne modificano i file
durante l'elaborazione. Nessun servizio di rete o telemetria nell'app.

## GitHub e licenza

Sorgenti e icona originali distribuiti con licenza MIT (`LICENSE`). Nessun codice
copiato dal progetto [MKV MultiMux GUI](https://github.com/MrTOgRaS/MKV-Multi-Mux-GUI),
consultato soltanto come riferimento funzionale. MKVToolNix ha una licenza propria:
vedere la sua distribuzione ufficiale prima di ridistribuirlo insieme all'app.

Per pubblicare: creare un repository `mkv-flow`, caricare il contenuto di questa
cartella (non `bin`, `build` o `test-output`) e attivare Actions. Repository ufficiale: https://github.com/Kyo89-sudo/mkv-flow. Nessun EXE MKVToolNix va incluso nel Git.

Fonti tecniche: [mkvmerge](https://mkvtoolnix.download/doc/mkvmerge.html),
[mkvpropedit](https://mkvtoolnix.download/doc/mkvpropedit.html),
[MSVC in Visual Studio 2026](https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc).

Per riconvertire il PNG dell'icona: Python + Pillow, `python tools/package_icon.py`.
Questi strumenti servono solo agli sviluppatori; ICO e PNG pronti sono inclusi.
Prompt e provenienza dell'asset: `assets/ICON-PROMPT.md`.
La versione del programma è definita in `src/version.h`.

