# 3310

Plugin audio (VST3 / AU) qui émule la voix d'un appel sur un **Nokia 3310**.
Toute l'interface est le téléphone : les touches **1 à 9** changent de mode, la touche **0** met le plugin en bypass (l'écran du Nokia s'éteint), et la **touche de droite** sous l'écran (flèches haut / bas) règle le mix : flèche haut pour monter, flèche bas pour baisser, par pas de 10 % (la molette marche aussi). L'écran affiche une barre « MIX » quelques secondes.

Par Underwater Palace.

## Modes

Tous les modes restent dans l'univers numérique / radio. Le mode **1** est l'effet de base du 3310.

| Touche | Mode | Ce que ça fait |
|---|---|---|
| 1 | CALL | Appel GSM : bande téléphonique 300-3400 Hz, codec type GSM 06.10 (trames de 20 ms, LPC, excitation RPE), compression douce |
| 2 | STUTTER | Boucles de buffer aléatoires (bégaiement numérique, parfois qui accélère) avant le codec |
| 3 | AM RADIO | Radio AM : détecteur à diode, fading lent |
| 4 | LOW BATT | Batterie qui meurt : échantillonnage réduit à 3 kHz, compression 6 bits, hauteur qui flotte, chutes de tension |
| 5 | WALKIE | Talkie-walkie : bande étroite 500-2600 Hz, compression brutale, squelch qui coupe le son entre les phrases |
| 6 | SCRAMBLE | Brouilleur de voix : inversion du spectre par modulation en anneau |
| 7 | BAD LINE | Mauvaise ligne : codec très grossier, pertes de paquets en rafales (remplacement de trame puis coupure) |
| 8 | CORRUPT | Données corrompues : décimation, 5 bits, bits qui sautent (uniquement quand il y a du signal), buffer qui se fige |
| 9 | ROBOT | Vocodeur LPC : voix robotique à hauteur fixe, bruit sur les passages non voisés |
| 0 | BYPASS | Son d'origine, retardé de la latence pour rester aligné, sans clic |

Aucun bruit n'est ajouté : pas de souffle, de statique ni de craquements, et le silence en entrée reste un silence en sortie. (Le niveau de ces bruits, supprimés par défaut, se règle avec la constante `kNoise` dans `Source/Dsp/Telephone.h`.)

Le mix est un vrai dry/wet : le signal d'origine est retardé de la latence, donc aucun filtre en peigne. Il est aussi automatisable dans le DAW (paramètre « Mix »).

Le signal est passé en mono (comme un vrai téléphone) dans les modes 1 à 9.

## Latence

Environ 20,7 ms (une trame GSM + le rééchantillonnage), identique dans tous les modes et en bypass. Le plugin la déclare à l'hôte, la compensation de latence du DAW l'aligne automatiquement.

## Compiler

Les builds Windows et macOS sortent tout seuls dans l'onglet **Actions** de GitHub (artefacts `3310-windows` et `3310-macos`).

En local, il faut CMake 3.22+ et un compilateur C++17 :

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target N3310_VST3
```

JUCE 8.0.15 est téléchargé automatiquement. Le VST3 se trouve ensuite dans `build/N3310_artefacts/Release/VST3/3310.vst3`.

### Installer

- **Windows** : copier le dossier `3310.vst3` dans `C:\Program Files\Common Files\VST3`, puis rescanner les plugins dans FL Studio.
- **macOS** : copier `3310.vst3` dans `/Library/Audio/Plug-Ins/VST3` (et `3310.component` dans `/Library/Audio/Plug-Ins/Components`). Les builds ne sont pas signés : si macOS bloque le plugin, lancer `xattr -cr /Library/Audio/Plug-Ins/VST3/3310.vst3`.

## Tests

- `tests/dsp_test.cpp` : test hors ligne du moteur audio (sans JUCE), rendu de tous les modes sur une voix de synthèse, niveaux, stabilité, latence. Lancé automatiquement par la CI.
- `tests/vst3_load_test.cpp` : charge le vrai fichier `3310.vst3` comme un DAW (instanciation, latence, paramètres, traitement de chaque mode, état, éditeur). Option `-DN3310_BUILD_TESTS=ON`, cible `N3310HostTest`.
- `tests/plugin_test.cpp` : test headless du vrai plugin (chemin audio, bypass, sauvegarde d'état, clics sur les touches, captures de l'interface). Option `-DN3310_BUILD_TESTS=ON`.

## Structure

```
Source/Dsp/Primitives.h   filtres, compresseur, mu-law, bruit
Source/Dsp/Codec.h        codec GSM-lite et vocodeur LPC
Source/Dsp/Telephone.h    moteur : rééchantillonnage 8 kHz, les 9 modes, bypass sans clic
Source/PixelArt.h         police 5x7 et emojis animés de l'écran
Source/PluginEditor.*     interface Nokia (écran LCD pixel + 12 touches)
Source/PluginProcessor.*  plugin JUCE, paramètre Mode (0 = bypass)
Assets/phone.png          photo du Nokia 3310
```

`preview/` contient la maquette web de l'interface (page HTML autonome), identique à l'éditeur du plugin.
