# Realistic Radio Aufträge+ / Jobs+ 1.0.0

**Deutsch** · [English](#english)

Zusatz-Plugin für **Realistic Radio 1.7.0 oder neuer**: **eigene Aufträge über das Handy** – Ware, Startfirma und Ziel selbst wählen. Nur im Einzelspieler.

## So geht's
1. `realistic-radio-jobs.dll` neben `realistic-radio.dll` in den `plugins`-Ordner kopieren, Spiel starten.
2. Handy öffnen (Umschalt + Einfg) → App **Aufträge+**. Das Handy liest kurz den neuesten Spielstand.
3. **Ware**, **Von** (Stadt → Firma; „Hier“ ist die Stadt, in der du gerade bist) und **Nach** wählen. In den Listen kann man tippen, um zu suchen.
4. **Auftrag anlegen** → **F5** drücken (Schnellspeichern). Das Handy trägt den Auftrag in diesen Spielstand ein.
5. **F9** drücken (Schnellladen) → der Auftrag steht im **Frachtmarkt der Startfirma**. Ein ganz normaler Auftrag mit Geld und Erfahrungspunkten.

## Gut zu wissen
- Das Plugin ersetzt dafür **einen** der Aufträge der Startfirma im Schnellspeicherstand. Anhänger, Menge und Gewicht kommen von einem Auftrag mit derselben Ware aus dem Spielstand. Sonst ändert sich nichts. Eine Kopie des Originals bleibt als `game.sii.rr-backup` im Ordner `quicksave` liegen.
- Die **Entfernung** stammt von Aufträgen zwischen denselben Städten. Gibt es keine, wird sie aus der Karte geschätzt. Danach richtet sich der Lohn.
- Es gehen nur Firmen und Waren, die es im Spielstand gibt. Die Startfirma muss gerade Aufträge haben.
- Spielstände in jedem Format gehen: verschlüsselt, binär oder Text. Der geänderte Spielstand wird unverschlüsselt geschrieben, das Spiel liest ihn genauso.
- **Nicht im Multiplayer** (TruckersMP, Haul MP …): Dort gibt es kein Schnellspeichern.
- Die Warennamen sind die internen Namen des Spiels (z. B. „Furniture“).
- Log: `realistic-radio.log`, Zeilen `Jobs+: …`.

---

## English

Add-on for **Realistic Radio 1.7.0 or newer**: **own jobs from the phone** – choose cargo, start company and destination yourself. Single player only.

1. Copy `realistic-radio-jobs.dll` next to `realistic-radio.dll` into the `plugins` folder and start the game.
2. Open the phone (Shift + Insert) → app **Jobs+**. The phone reads the newest save for a moment.
3. Choose **Cargo**, **From** (city → company; "Here" is the city you are in) and **To**. Type to search in the lists.
4. **Create job** → press **F5** (quick save). The phone puts the job into that save.
5. Press **F9** (quick load) → the job is in the **freight market of the start company**. It is a normal job with pay and experience.

Good to know:
- The add-on replaces **one** of the start company's offers in the quick save. Trailer, amount and weight come from an offer of the same cargo in the save. Nothing else changes, and a copy of the original stays as `game.sii.rr-backup` in the `quicksave` folder.
- The distance (and so the pay) comes from offers between the same cities. Without such offers it is estimated from the map.
- Only companies and cargo that exist in the save are possible, and the start company has to have offers.
- Saves in every format work (encrypted, binary, text). The changed save is written unencrypted, and the game reads it the same way.
- Not in multiplayer (TruckersMP, Haul MP …), as there is no quick save there.
- Log: `realistic-radio.log`, lines `Jobs+: …`.

License: MPL 2.0 · © 2026 ScufyKing · uses tiny-AES-c (public domain) and miniz (MIT).
