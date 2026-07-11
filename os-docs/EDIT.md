# EDIT (MEDIT)

Norwegian text editor for HarvaC, targeting IBM 5155 Portable PC
(8088, 256 kB, CGA). Fits in a COM file (~15 kB), installed as
`BIN/EDIT.COM` on the disk image.

## Bruk / Usage

    EDIT [/F] [FIL.TXT]

    /F   hopp over CGA-snegle-venting (raskere pa kloner og emulatorer)
    /F   skip CGA snow wait (faster on clones and emulators)

Uten argument apnes et tomt dokument. Med filnavn: filen lastes (eller
opprettes hvis den ikke finnes).

Without an argument an empty document is opened. With a filename: the file
is loaded (or created if it does not exist).

## Tastatursnarveier / Keyboard shortcuts

| Tast | Handling |
|------|---------|
| Alt-F | Fil-meny (Ny, Apne, Lagre, Lagre som, Avslutt) |
| Alt-R | Rediger-meny (Klipp, Kopier, Lim inn, Tekstbryting) |
| Alt-S | Sok-meny (Finn, Finn neste, Erstatt) |
| Esc | Lukk meny / avbryt dialog |
| F3 | Finn neste |
| Ctrl+S | Lagre |
| Ctrl+Q | Avslutt |
| Shift+Del | Klipp ut |
| Ctrl+Ins | Kopier |
| Shift+Ins | Lim inn |
| Shift+piltaster | Marker tekst |
| Ctrl+Home/End | Hopp til starten/slutten av dokumentet |

## Bygge / Build

Bygges automatisk av prosjektbygget (OpenWatcom V2 i `/opt/watcom`):

    make            # kjorer build.py; kompilerer apps/medit/ til build/EDIT.COM

Resultatet legges pa diskbildet som `BIN/EDIT.COM`.

## Arkitektur

Se `docs/wiki/medit-harvac.md` for kode-kart, begrensninger fra IBM
5155-maskinvaren (CGA-sno, 8088, cp865) og porteringsavgjorelser
(HarvaC INT 40h-syscalls i stedet for DOS INT 21h).

## Testing

Kjor HarvaC i QEMU og start EDIT fra skjellet:

    make run        # bygg + QEMU
    /> EDIT TEST.TXT
