# Realistic Radio Bus+ 1.1.0

**Deutsch** · [English](#english)

Zusatz-Plugin für **Realistic Radio 1.7.0 oder neuer** und das DLC **Coaches** (Reisebusse) in Euro Truck Simulator 2. Nur im Einzelspieler.

Die App **Bus+** im Handy kann zwei Sachen:
- **Fahrplan unterwegs:** nächster Halt, Ankunft laut Plan, Ankunft laut Navi mit Verspätung, Fahrgäste und Zufriedenheit.
- **Lenkzeit:** wie lange du noch fahren darfst und ob das bis zum nächsten Halt reicht.
- **Fahrtenbuch mit Fahrtbericht:** nach jeder Fahrt Pünktlichkeit an jedem Halt, Fahrgäste, Zufriedenheit und Lohn.
- **Eigene Linie bauen:** Start, Halte (oder ein Ausflugsziel) und Abfahrtszeit selbst wählen. Die Linie landet dann in den Vertragsfahrten.

## Einbauen
`realistic-radio-bus.dll` neben `realistic-radio.dll` in den `plugins`-Ordner kopieren, Spiel starten. Handy öffnen (Umschalt + Einfg) → App **Bus+**.

## Fahrplan
- Nimm im Spiel eine Vertragsfahrt an. Bus+ zeigt die Linie, den **nächsten Halt**, die **Ankunft laut Plan** und die **Ankunft laut Navi**. Daraus ergibt sich, ob du pünktlich, zu früh oder zu spät bist.
- Dazu kommen die Strecke bis zum Halt, die **Fahrgäste** an Bord, wie viele aus- und einsteigen, und ihre **Zufriedenheit**.
- **Halte** zeigt alle Halte mit Plan- und Ankunftszeiten.
- **Hinweise auf dem Navi** (abschaltbar):
  - wenn du eine Fahrt annimmst,
  - nach jedem Halt (nächster Halt),
  - wenn du mehr als 10 Minuten Verspätung hast.

## Lenkzeit
- Unter **Lenkzeit** steht, wie lange du noch fahren darfst, bis du eine Pause brauchst (aus dem Spiel, nur wenn die Müdigkeit eingeschaltet ist).
- Bus+ vergleicht das mit dem Weg bis zum nächsten Halt und bis zum Ziel: „reicht bis zum Ziel“, „reicht bis Milano, danach Pause einplanen“ oder „Pause vor Milano nötig – Rastplatz einplanen“.
- Reicht die Lenkzeit nicht bis zum nächsten Halt, kommt ein Hinweis aufs Navi, bei 60 und bei 15 Minuten Lenkzeit.

## Fahrtenbuch und Fahrtbericht
- **Fahrten und Fahrtberichte** listet alle Busfahrten des Profils mit Lohn, Strecke und Zufriedenheit, dazu die Summe.
- Für jede Fahrt, die Bus+ mitverfolgt hat, gibt es einen **Fahrtbericht**:
  - der Fahrplan, wie er bei der Abfahrt war (Abfahrt + Fahrzeiten + 15 Minuten an jedem Halt),
  - an jedem Halt: geplante und echte Zeit, pünktlich (5 Minuten zu früh bis 3 Minuten zu spät), zu früh oder verspätet, wer aus- und eingestiegen ist,
  - Ergebnis: Lohn, Zufriedenheit, Fahrgäste, Strecke, wie viele Halte pünktlich waren.
- Wenn die Fahrt endet, kommt eine Zusammenfassung aufs Navi.
- Fahrten von vor Bus+ stehen ohne Bericht im Fahrtenbuch. Die Berichte liegen in `realistic-radio\bus-trips.json`.

Die Halte und Fahrgäste stehen im Spielstand. Das Spiel speichert nach jedem Halt und alle paar Minuten während der Fahrt automatisch, und Bus+ liest den neuesten Spielstand dann neu. Die Ankunft laut Navi kommt laufend aus dem Spiel.

## Eigene Linie
1. **Linie planen** → **Haltestelle** (Start): am besten die, an der du gerade bist („Hier“). Dort nimmst du die Linie an. Es gehen nur Haltestellen, an denen gerade Linien angeboten werden.
2. **Art:**
   - **Linie:** bis zu 6 Halte.
   - **Ausflug:** zu einem Ausflugsziel und zurück zum Start.
3. **+ Halt hinzufügen**: Oben stehen Städte mit bekannter Strecke vom letzten Halt, darunter alle anderen (Entfernung geschätzt). Ein Halt lässt sich antippen, um ihn zu entfernen oder zu verschieben.
4. **Uhrzeit** der Abfahrt wählen (jeden Tag, in 15-Minuten-Schritten).
5. **Linie anlegen** → **F5** drücken (Schnellspeichern). Das Handy trägt die Linie in diesen Spielstand ein.
6. **F9** drücken (Schnellladen) → die Linie steht in den **Vertragsfahrten** der Start-Haltestelle, einen Spieltag lang.

## Gut zu wissen
- Für die Linie ersetzt das Plugin **eine** der Linien der Start-Haltestelle im Schnellspeicherstand. Den Bus für die Schnell-Fahrt übernimmt es von dieser Linie. Sonst ändert sich nichts. Eine Kopie des Originals kommt nach `plugins\realistic-radio\backup`.
- **Entfernung und Fahrzeit** jeder Etappe kommen aus den Linien im Spielstand.
  - Kam eine Etappe dort nicht vor, wird sie über andere Halte oder aus der Karte geschätzt („ca.“).
  - Bus+ merkt sich alle Etappen, die es einmal gesehen hat (`realistic-radio\bus-legs.json`), so werden es mit der Zeit immer mehr bekannte Strecken.
  - Ohne jede bekannte Verbindung (z. B. nach Island) geht der Halt nicht.
- **Steam Cloud:** Profile in der Steam Cloud gehen auch. Der geänderte Spielstand wird dann über Steam gespeichert, sonst würde das Spiel ihn nicht laden („Fehler beim Laden des Spielstands“). Klappt das ausnahmsweise nicht, sagt das Handy Bescheid; dann im Spiel beim Profil „Steam Cloud verwenden“ ausschalten.
- Spielstände in jedem Format gehen: verschlüsselt, binär oder Text. Der geänderte Spielstand wird unverschlüsselt geschrieben, das Spiel liest ihn genauso.
- **Nicht im Multiplayer** (TruckersMP, Haul MP …), dort gibt es kein Schnellspeichern. Der Fahrplan braucht den Spielstand und geht deshalb auch nur im Einzelspieler.
- Nur für Euro Truck Simulator 2 (das Coaches-DLC gibt es nur dort).
- Einstellungen: `realistic-radio\bus.json`. Log: `realistic-radio.log`, Zeilen `Bus+: …`.

---

## English

Add-on for **Realistic Radio 1.7.0 or newer** and the **Coaches** DLC of Euro Truck Simulator 2. Single player only.

The phone app **Bus+** does two things:
- **Timetable on the road:** next stop, planned arrival, arrival by the route advisor with the delay, passengers and satisfaction.
- **Driving time:** how long you may still drive and whether that is enough to the next stop.
- **Logbook with trip reports:** after every job the punctuality at each stop, passengers, satisfaction and pay.
- **Build your own line:** choose start, stops (or a viewpoint) and departure time. The line then shows up in the contract jobs.

Install: copy `realistic-radio-bus.dll` next to `realistic-radio.dll` into the `plugins` folder and start the game. Open the phone (Shift + Insert) → app **Bus+**.

**Timetable**
- Take a contract job in the game. Bus+ shows the line, the next stop and the arrival by plan and by the route advisor, so you see whether you are on time, early or late.
- It also shows the distance, the passengers on board, how many get off and on, and their satisfaction.
- **Stops** lists every stop with its times.
- Notices on the navigation screen (can be switched off):
  - when you take a job,
  - after every stop,
  - when you are more than 10 minutes late.

**Driving time**: how long you may drive until a rest (from the game, only with fatigue on), compared with the way to the next stop and to the end: "enough to the end", "enough to Milano, plan a rest after it" or "rest needed before Milano". A notice comes at 60 and 15 minutes when it is not enough to the next stop.

**Logbook and trip report**: **Jobs and reports** lists every bus job of the profile with pay, distance and satisfaction. Every job Bus+ followed has a report: the timetable as it was at the start (departure + stage times + 15 minutes at each stop), the planned and real time at each stop (on time = 5 minutes early to 3 minutes late), who got off and on, and the result. A summary comes to the navigation when the job ends. Reports: `realistic-radio\bus-trips.json`.

Stops and passengers come from the save. The game saves after every stop and every few minutes while driving, and Bus+ then reads it again. The route advisor arrival comes from the game all the time.

**Own line**
1. **Plan a line** → **Stop** (the start): best the one you are at ("Here"), because you take the line there. Only stops with lines on offer are possible.
2. **Kind:**
   - **Line:** up to 6 stops.
   - **Excursion:** to a viewpoint and back to the start.
3. **+ Add a stop**: known legs from the last stop come first, the others below (estimated). Tap a stop to remove or move it.
4. Choose the departure **time** (every day, 15-minute steps).
5. **Create the line** → press **F5** (quick save). The phone puts the line into that save.
6. Press **F9** (quick load) → the line is in the **contract jobs** of the start stop for one game day.

Good to know:
- The add-on replaces **one** line offer of the start stop in the quick save, and the bus for the quick job comes from that offer. Nothing else changes, and a copy of the original goes to `plugins\realistic-radio\backup`.
- Distance and time of each leg come from the line offers in the save.
  - Legs that no offer had are estimated over other stops or from the map ("about").
  - Bus+ remembers every leg it has seen (`realistic-radio\bus-legs.json`), so more legs become known over time.
  - A stop without any known connection (e.g. Iceland) is not possible.
- Steam Cloud profiles work too: the changed save is then written through Steam, otherwise the game could not load it. If that ever fails, the phone says so; then turn off "Use Steam Cloud" for the profile in the game.
- Saves in every format work (encrypted, binary, text). The changed save is written unencrypted, and the game reads it the same way.
- Not in multiplayer (no quick save there). The timetable needs the save, so it is single player only too. ETS2 only.
- Settings: `realistic-radio\bus.json`. Log: `realistic-radio.log`, lines `Bus+: …`.

License: MPL 2.0 · © 2026 ScufyKing · uses tiny-AES-c (public domain) and miniz (MIT).

## Changelog
- **1.1.0:** Driving time until the next rest (with notices), logbook with a trip report for every job (punctuality per stop, passengers, pay).
- **1.0.1:** Fix: "Fehler beim Laden des Spielstands" after F9 with Steam Cloud profiles – the changed save is now written through Steam. Backup moved to `plugins\realistic-radio\backup`.
- **1.0.0:** First version.
