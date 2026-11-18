# Image-to-Sound Synthesizer

Dieses Projekt verwandelt ein Bild in ein Spektrogramm und rekonstruiert daraus
eine WAV-Datei, die anschließend z. B. in Ableton Live importiert werden kann.

## Pipeline

```text
Foto
  ↓
Graustufen
  ↓
Gamma / Kontrast / Blur / Invertierung
  ↓
logarithmische Frequenzzuordnung
  ↓
Magnitude-Spektrogramm
  ↓
Griffin-Lim
  ↓
WAV
  ↓
Ableton Live
```

## Installation

Python 3.10+ wird empfohlen.

```bash
pip install -r requirements.txt
```

## Verwendung

1. Bild nach `data/images/` kopieren.
2. In `main.py` `IMAGE_PATH` einstellen.
3. Parameter im CONFIG-Bereich ändern.
4. Starten:

```bash
python main.py
```

Die Dateien werden unter `data/output/` erzeugt:

```text
testneu1.wav
testneu1_spec.png
```

## Wichtige Parameter

### Bild

`GAMMA`

- `0.3` – dunkle Bildbereiche werden stärker hörbar
- `0.5` – guter Ausgangspunkt
- `1.0` – neutral
- `2.0` – dunkle Bereiche werden stärker unterdrückt

`BLUR`

Glättet das Bild vor der Umwandlung.

`CONTRAST`

Verändert den Kontrast.

`INVERT`

Kehrt Schwarz und Weiß um.

`FLIP_HORIZONTAL`

Dreht das Bild zeitlich um.

`FLIP_VERTICAL`

Spiegelt das Bild vertikal. Für die normale Bildinterpretation sollte das
aktiviert bleiben.

### Frequenz

`FMIN` und `FMAX`

Bestimmen den musikalisch relevanten Frequenzbereich.

Beispiel:

```python
FMIN = 40
FMAX = 8000
```

`FREQUENCY_SCALE = "log"`

Das ist für musikalische Ergebnisse empfehlenswert.

Bei logarithmischer Zuordnung entsprechen gleiche vertikale Abstände
annähernd gleichen musikalischen Intervallen.

Zum Vergleich:

```python
FREQUENCY_SCALE = "linear"
```

### Zeit

Die horizontale Bildachse entspricht der Zeit.

Die ungefähre Länge ergibt sich aus:

```text
WIDTH × HOP_LENGTH / SAMPLE_RATE
```

Bei:

```python
WIDTH = 1024
HOP_LENGTH = 256
SAMPLE_RATE = 44100
```

entstehen ungefähr 5.95 Sekunden Audio.

### Dynamic Range

`DYNAMIC`

Beispiel:

```python
DYNAMIC = 40
DYNAMIC = 60
DYNAMIC = 80
```

60 dB ist ein guter Startpunkt.

### Griffin-Lim

`GRIFFIN_LIM_ITERATIONS`

Mehr Iterationen können die Rekonstruktion verbessern, kosten aber Zeit.

Gute Ausgangswerte:

```python
32
64
128
```

Der erzeugte Sound ist nicht identisch mit einem Audio-Signal, das ursprünglich
dieses Spektrogramm erzeugt hätte, weil das Bild keine Phaseninformation enthält.
Griffin-Lim schätzt diese Phase.

## Ableton

Die erzeugte WAV-Datei kann direkt in Ableton Live gezogen werden.

Besonders interessant ist:

- WAV als Audio Clip
- Warp ausschalten oder bewusst einsetzen
- Clip in Simpler/Sampler laden
- einzelne Frequenzbereiche separat erzeugen
- verschiedene Gamma-/Dynamic-Presets als Layer verwenden
- kurze Bilder/Sound-Segmente als One-Shots verwenden
- lange Bilder als atmosphärische Texturen verwenden

## Kreative Presets

### Clean

```python
GAMMA = 1.0
BLUR = 0.0
CONTRAST = 1.0
DYNAMIC = 60
FMIN = 40
FMAX = 8000
```

### Dark / Texture

```python
GAMMA = 0.35
BLUR = 2.0
CONTRAST = 1.2
DYNAMIC = 70
FMIN = 30
FMAX = 12000
```

### Bright / Percussive

```python
GAMMA = 1.5
BLUR = 0.0
CONTRAST = 1.8
DYNAMIC = 50
FMIN = 100
FMAX = 12000
```

## Nächste sinnvolle Erweiterungen

Das Projekt lässt sich danach gut erweitern um:

1. automatische Ziel-Dauer statt indirekter Dauer über WIDTH
2. verschiedene Frequenz-Skalen
3. Mel-Skala
4. Bark-Skala
5. Stereo-Synthese
6. Panorama aus horizontalen Bildinformationen
7. mehrere Frequenz-Bänder als getrennte WAV-Dateien
8. Pitch-Shifting
9. Time-Stretching
10. Preset-Dateien
11. Batch-Verarbeitung mehrerer Bilder
12. direkte Ableton-orientierte One-Shot- und Loop-Erzeugung
