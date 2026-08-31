# ULT: Ultimate Latency Tester (DirectX 12)

<p align="center">
  <strong>Benchmark & Diagnostica ad Altissime Prestazioni per la Misurazione della Latenza dei Controller su Windows</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/DirectX-12-0078D4?style=for-the-badge&logo=windows&logoColor=white" alt="DirectX 12" />
  <img src="https://img.shields.io/badge/C%2B%2B-17%20MSVC-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++17" />
  <img src="https://img.shields.io/badge/Polling%20Rate-1000Hz%20%2F%202000Hz-00E5FF?style=for-the-badge" alt="Polling Rate" />
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20(x64)-00D26A?style=for-the-badge" alt="Platform" />
</p>

---

## 📖 Panoramica

**ULT (Ultimate Latency Tester)** è un'applicazione nativa per Windows in **DirectX 12** progettata per misurare con precisione sub-millisecondo la latenza di risposta reale (Input Lag totale) di qualsiasi controller da gioco quando si sposta la levetta analogica dal centro a 90° a destra.

Il motore grafico a bassissimo overhead e la pipeline multithread a **2000Hz** garantiscono framerate estremi (oltre 500+ FPS) per isolare l'hardware ed eliminare i colli di bottiglia di CPU e GPU.

---

## ⚡ Caratteristiche Principali

* **Motore DirectX 12 Low-Latency**: Utilizza `DXGI_SWAP_EFFECT_FLIP_DISCARD` e DirectFlip per azzerare il buffering del compositore di Windows (DWM).
* **Ambiente 3D Raymarched Procedurale**: Pavimento a scacchiera prospettico, mirino centrale rosso ad alta definizione e linea target verde smeraldo a 90°.
* **Cronometro Sub-Millisecondo ad Alta Risoluzione**: Basato su `QueryPerformanceCounter` (QPC), parte al primo impulso oltre la deadzone e si arresta al raggiungimento esatto dei 90°.
* **Thread di Polling Input Dedicato a 2000Hz**: Interrogazione asincrona non bloccante dei pacchetti hardware grezzi.
* **Device Watcher Asincrono a 0 Stutter**: Riconnessione e disconnessione USB/Bluetooth fluida senza cali di framerate.
* **Controllo Hardware Lightbar DualShock 4 & DualSense 5**: Impostazione automatica via report HID sul colore Cyan Elettrico / Blu Neon.
* **Interfaccia Grafica Moderna (Dark Frosted Glass)**: Card in vetro acrilico scuro, chip opzione segmentati, badge telemetrici in tempo reale e bandiere di selezione lingua (Italiano 🇮🇹 / English 🇬🇧).

---

## 📊 Telemetria Misurata in Tempo Reale

| Metrica | Descrizione |
| :--- | :--- |
| **FPS & Frametime D3D12** | Framerate reale del motore di rendering e tempo di scansione GPU. |
| **Polling Rate USB/BT** | Frequenza effettiva di invio pacchetti del controller in Hz (es. 250Hz, 500Hz, 1000Hz). |
| **Latenza Input Controller** | Ritardo del ciclo di polling del controller (1000 / Hz). |
| **Latenza Render D3D12** | Tempo di composizione ed emissione del frame DirectX 12. |
| **Latenza Monitor Display** | Tempo medio di scansione dello schermo calcolato sui Hz reali del monitor (1000 / (2 × Hz)). |
| **Latenza Hardware Levetta** | Ritardo fisico del sensore (Potenziometro / Hall Effect / TMR), inseribile da 0 a 100 ms. |
| **Jitter + Ritardo Umano** | Variazione della curva di velocità del pollice umano rispetto al limite teorico. |
| **Tempo Record (Best)** | Miglior tempo di reazione assoluto registrato durante la sessione. |

---

## 🎮 Controller Supportati per Modalità di Input

ULT supporta qualsiasi gamepad collegato via **Cavo USB**, **Bluetooth** o **Ricevitore Wireless**, suddivisi in 4 modalità di input:

```
                              ┌─── 1. SONY RAWINPUT (1000Hz Nativo - DualShock 4 / DualSense 5)
                              ├─── 2. MICROSOFT XINPUT (Xbox Series X|S / Xbox One / Xbox 360)
  ULT INPUT SUBSYSTEM ────────┼─── 3. DIRECTINPUT 8 (Retro Pad, Arcade Stick, Volanti)
                              └─── 4. AUTO-DETECT (Priorità automatica alla latenza minore)
```

### 1. 🔵 Sony Native RawInput (HID a 1000Hz)
*Bypassa completamente i layer intermedi di Windows, leggendo i pacchetti HID grezzi da 64-byte a 1000Hz nativi con supporto Lightbar RGB.*
* **Sony PlayStation DualSense 5** (USB & Bluetooth)
* **Sony PlayStation DualSense 5 Edge** (USB & Bluetooth)
* **Sony PlayStation DualShock 4 v1** (CUH-ZCT1, USB & Bluetooth)
* **Sony PlayStation DualShock 4 v2** (CUH-ZCT2, USB & Bluetooth)
* **Sony DualShock 4 USB Wireless Adapter Ufficiale**
* Controller Custom e Pro-Gaming basati su chip Sony (Scuf Reflex, Nacon Revolution, Razer Raiju in modalità PS4/PS5).

### 2. 🟢 Microsoft XInput
*Integrazione nativa a bassa latenza per l'intero ecosistema Xbox e controller Windows compatibili.*
* **Xbox Wireless Controller (Series X|S)** (USB, Bluetooth, Xbox Wireless Adapter)
* **Xbox Elite Wireless Controller Series 2 & Series 1**
* **Xbox One Wireless Controller** (Revisioni Standard, S, X)
* **Xbox 360 Controller** (Wired & Wireless)
* Tutti i controller XInput di terze parti:
  * Scuf Instinct / Envision
  * Razer Wolverine (V2, V2 Pro, Chroma)
  * Thrustmaster eSwap X / S Pro
  * GameSir (G7 SE, T4k, Kaleid)
  * 8BitDo Ultimate (Versione Xbox / 2.4G)
  * PDP Rematch, Victrix Gambit, PowerA Fusion Pro, Flydigi Vader/Apex (in modalità XInput).

### 3. 🟣 DirectInput 8 (Fallback Universale)
*Supporto esteso per periferiche legacy, arcade stick e controller personalizzati.*
* Fightstick e Arcade Stick (Sanwa, Hori Fighting Commander, Qanba, Mayflash)
* Volanti e pedaliere da simulazione (Logitech G29/G920/G923, Thrustmaster T300/TX, Fanatec)
* Gamepad retro e generici USB/Bluetooth (8BitDo in modalità DInput, iPega, SteelSeries, ecc.).

### 4. ⚡ Auto-Detect (Consigliata)
* Rileva all'istante il controller collegato e assegna automaticamente il backend a latenza più bassa (**Sony RawInput 1000Hz > XInput > DirectInput**).

---

## 🕹️ Comandi e Scorciatoie

### Da Controller:
* **`OPTIONS` / `START`**: Apri / Chiudi Menu Impostazioni
* **`D-PAD (Frecce Su/Giù)`**: Naviga tra le voci del menu
* **`D-PAD (Frecce Sinistra/Destra)`**: Modifica il valore dell'opzione selezionata
* **`✕` (PlayStation) / `A` (Xbox)**: Conferma selezione
* **`◯` (PlayStation) / `B` (Xbox)**: Chiudi menu / Annulla
* **`□` (PlayStation) / `X` (Xbox)**: Cambia levetta attiva da testare (**L3 Sinistra** ⇄ **R3 Destra**)
* **`△` (PlayStation) / `Y` (Xbox)**: Reset cronometro e record

### Da Tastiera:
* **`ESC`**: Apri / Chiudi Menu Impostazioni
* **`Frecce Direzionali (↑ / ↓ / ← / →)`**: Navigazione e modifica impostazioni
* **`INVIO`**: Conferma selezione
* **`Q`**: Cambia levetta attiva da testare (**L3** ⇄ **R3**)
* **`R`**: Reset cronometro e record
* **`0 - 9` (Tastierino Numerico)**: Digitazione diretta della latenza hardware levetta (0 - 100 ms)
* **`F11`**: Alterna Schermo Intero / Finestra Desktop

---

## ⚙️ Opzioni di Configurazione nel Menu

1. **Limite FPS**: `30`, `60`, `120`, `144`, `180`, `240`, `500`, `Illimitato`
2. **Risoluzione**: `1080p`, `900p`, `720p`, `540p`
3. **Qualità Grafica**: `Minima (Ultra Fast)`, `Media`, `Alta`
4. **Tipo Collegamento**: `Auto`, `DS4 Nativo`, `DS5 Nativo`, `XInput`, `DirectInput`
5. **Modalità Display**: `Finestra (Desktop)`, `Fullscreen Esclusivo (DirectFlip)`
6. **V-Sync**: `Disabilitato (Latenza Minima)`, `Abilitato`
7. **Lingua**: `Italiano 🇮🇹`, `English 🇬🇧`
8. **Curva Risposta Levetta**: `Lineare (Analogica)`, `Istantanea (Max Speed Digitale)`
9. **Latenza Hardware Levetta**: Regolabile o digitabile da tastiera (0 - 100 ms)

---

## 🛠️ Come Compilare dal Codice Sorgente

### Requisiti:
* **Windows 10 / 11 a 64-bit**
* **Visual Studio 2019 / 2022** con il carico di lavoro *"Sviluppo di applicazioni desktop con C++"* (MSVC v142/v143 e Windows 10/11 SDK).

### Compilazione a un clic:
1. Clona il repository:
   ```bash
   git clone https://github.com/tuo-username/ULT-Ultimate-Latency-Tester.git
   cd ULT-Ultimate-Latency-Tester
   ```
2. Esegui lo script:
   ```bat
   build.bat
   ```
3. Verrà generato l'eseguibile unico autonomo **`ULT Ultimate Latency Tester.exe`** pronto all'uso!

---

## 📄 Licenza

Questo progetto è distribuito sotto licenza **MIT** (o licenza a scelta). Consulta il file `LICENSE` per i dettagli.

<p align="center">
  <em>Sviluppato con passione per la community di gaming, esports e hardware tuning.</em>
</p>