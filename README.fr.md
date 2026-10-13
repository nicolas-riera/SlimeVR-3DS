# SlimeVR 3DS

*[English version](README.md)*

Homebrew qui transforme une console de la famille Nintendo 3DS (3DS, 3DS XL, 2DS, New 3DS, New 3DS XL, New 2DS XL) en tracker [SlimeVR](https://slimevr.dev/) Wi-Fi, à partir de son gyroscope et de son accéléromètre.

- Le serveur SlimeVR est trouvé automatiquement. Il n'y a pas d'IP à saisir.
- Le suivi continue console fermée : la mise en veille est bloquée, les écrans s'éteignent et le Wi-Fi reste verrouillé.
- Une interface simple affiche l'état de la connexion, l'état du gyroscope, un aperçu 3D de la console et la courbe des vitesses angulaires.
- L'interface est en anglais, ou en français quand la console est réglée en français.

## Installation

| Fichier | Usage |
|---|---|
| `dist/SlimeVR-3DS.3dsx` | à copier dans `sdmc:/3ds/`, puis à lancer depuis le Homebrew Launcher |
| `dist/SlimeVR-3DS.cia` | à installer avec FBI (ou un autre installateur), l'icône apparaît dans le menu HOME |
| `dist/SlimeVR-3DS.3ds` | pour linker / flashcart |

## Utilisation

1. Activer le Wi-Fi de la console (elle doit déjà connaître ton réseau) et lancer le serveur SlimeVR sur le PC.
2. Lancer l'application et poser la console à plat environ 2 secondes, le temps de la calibration du gyroscope.
3. La console cherche le serveur toute seule. Au premier lancement, SlimeVR peut demander d'**accepter le nouveau tracker**.
4. Fixer la console sur le corps, puis faire les resets habituels (full reset, mounting) dans SlimeVR.
5. On peut fermer la console : le suivi continue.

Commandes : **A** recentre l'aperçu 3D (affichage uniquement), **SELECT** éteint ou rallume les écrans, **START** quitte.

### Découverte du serveur

Plusieurs méthodes tournent en parallèle :

- un handshake envoyé en broadcast (`255.255.255.255` et le broadcast du sous-réseau) ;
- l'écoute du port 6969, où le serveur SlimeVR diffuse lui-même des paquets quand il n'a aucun tracker ;
- le dernier serveur connu, mémorisé dans `sdmc:/3ds/SlimeVR-3DS/server.txt` ;
- au bout de 3 s, un scan unicast du sous-réseau local (/24 au maximum si le réseau est plus grand).

Si rien ne marche (réseau isolé, VLAN…), il suffit d'écrire l'IP du PC dans `server.txt`.

## Fonctionnement

- Les échantillons du gyroscope (~100 Hz) et de l'accéléromètre sont lus directement dans la mémoire partagée HID.
- Un filtre de Mahony 6 axes calcule l'orientation. Le biais du gyroscope est calibré au démarrage puis corrigé à chaque phase de repos. Sans magnétomètre, le cap dérive lentement : c'est le même comportement que les trackers SlimeVR 6 axes, et les resets SlimeVR le compensent.
- Le protocole est le protocole UDP natif de SlimeVR (port 6969) : handshake, sensor info, rotation (paquet 17), heartbeat, ping, batterie et signal Wi-Fi.
- Le thread capteurs/réseau est indépendant de l'interface. Quand le couvercle est fermé (ou après SELECT), l'interface n'est plus dessinée et le rétroéclairage est coupé.
- Wi-Fi console fermée : l'application prend l'état exclusif « infrastructure » de NDM, le verrouille et suspend les démons StreetPass/SpotPass. Si la connexion tombe quand même, une reconnexion est relancée toutes les 10 s.

## Compilation

Il faut devkitPro (`3ds-dev` : devkitARM, libctru, citro2d, citro3d), ainsi que `makerom` et `bannertool` dans le PATH.

```sh
./build.sh            # ou: make
# avec des outils hors PATH :
DEVKITPRO=/opt/devkitpro CTRTOOLS=/chemin/vers/makerom_et_bannertool ./build.sh
```

Les icônes, la bannière et le jingle sont générés par `python3 tools/gen_assets.py` (uniquement la bibliothèque standard).

Test du filtre sur PC :

```sh
gcc -O2 -Isource tests/fusion_test.c source/fusion.c -lm -o fusion_test && ./fusion_test
```
