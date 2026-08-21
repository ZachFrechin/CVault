# VaultCLI

VaultCLI est un gestionnaire de mots de passe local en interface terminal (TUI). Les dossiers et les entrées sont conservés dans un vault chiffré avant chaque écriture.

## État de la V1

La V1 permet de :

- créer un dossier et des entrées depuis l'interface terminal ;
- rechercher une entrée dans tous les dossiers ;
- afficher ou masquer un mot de passe ;
- copier un login ou un mot de passe dans le presse-papiers ;
- supprimer une entrée avec sauvegarde chiffrée immédiate ;
- charger et sauvegarder un vault binaire versionné.

L'import/export dédié, l'édition avancée, la suppression de dossiers et le générateur de mots de passe ne sont pas encore inclus. Une copie manuelle du fichier de vault reste possible, mais aucun format d'échange ou mécanisme de fusion n'est fourni.

## Prérequis

- C++17 ;
- CMake 3.21 ou plus récent ;
- Ninja sur Linux/macOS ; Visual Studio 2022 sur Windows ;
- un vcpkg installé et la variable `VCPKG_ROOT` configurée ;
- les dépendances déclarées dans `vcpkg.json` : libsodium, Catch2 et FTXUI (récupérée par CMake).

## Compiler et tester

Depuis la racine du projet :

```sh
cmake --preset vcpkg-debug
cmake --build --preset vcpkg-debug -j2
ctest --test-dir build-vcpkg --output-on-failure
```

La première configuration peut télécharger FTXUI. Le binaire est produit dans `build-vcpkg/vault`.

Sous Windows, le preset utilise Visual Studio 2022 afin de garder la même ABI que le triplet vcpkg `x64-windows` :

```powershell
cmake --preset vcpkg-release-windows
cmake --build --preset vcpkg-release-windows --parallel 2
ctest --test-dir build-vcpkg-release -C Release --output-on-failure
```

## Installer `vault` depuis une release

Les workflows GitHub Actions compilent et testent le projet sur Linux, macOS Intel, macOS Apple Silicon et Windows. Une release est publiée lorsqu'un tag `v*` est poussé :

```sh
git tag v0.1.0
git push origin v0.1.0
```

Sur macOS ou Linux, l'installateur vérifie l'archive et son SHA-256, installe `vault` dans `~/.local/bin`, puis ajoute ce dossier au profil du shell :

```sh
curl --proto '=https' --tlsv1.2 -fsSL \
  https://raw.githubusercontent.com/ZachFrechin/CVault/main/packaging/install.sh | sh
```

Ouvrez un nouveau terminal après l'installation, puis vérifiez :

```sh
vault --help
```

Pour installer une version précise, utilisez `VAULT_VERSION=0.1.0`. Pour choisir un autre dossier, utilisez `VAULT_INSTALL_DIR`; `VAULT_NO_PATH=1` désactive la modification automatique du profil.

Sous Windows, téléchargez et exécutez le script PowerShell :

```powershell
Invoke-WebRequest `
  https://raw.githubusercontent.com/ZachFrechin/CVault/main/packaging/install.ps1 `
  -OutFile install.ps1
PowerShell -ExecutionPolicy Bypass -File .\install.ps1
```

Le binaire est installé dans `%LOCALAPPDATA%\VaultCLI\bin` et ce dossier est ajouté au `PATH` utilisateur. Les archives publiées sont `vault-linux-x64.tar.gz`, `vault-macos-x64.tar.gz`, `vault-macos-arm64.tar.gz` et `vault-windows-x64.zip`.

## Démarrer l'application

Le mot de passe maître n'est jamais accepté en argument. Il est demandé dans une invite masquée.

```sh
# Utiliser le vault personnel par défaut
./build-vcpkg/vault

# Même comportement, forme explicite
./build-vcpkg/vault --vault

# Utiliser un fichier précis (le dossier parent doit déjà exister)
./build-vcpkg/vault --vault ./data/mon.vault

# Afficher l'aide sans accéder au vault
./build-vcpkg/vault --help
```

Le vault par défaut est créé automatiquement avec son dossier applicatif :

- macOS : `~/Library/Application Support/VaultCLI/default.vault` ;
- Linux : `$XDG_DATA_HOME/vaultcli/default.vault`, ou `~/.local/share/vaultcli/default.vault` ;
- Windows : `%APPDATA%\\VaultCLI\\default.vault`.

Lors du premier lancement, deux saisies identiques sont demandées. Un fichier existant n'est jamais remplacé si son format, son intégrité ou le mot de passe sont invalides.

## Raccourcis TUI

- `←` / `→` : changer de panneau ; `↑` / `↓` : sélectionner ; `Entrée` : ouvrir un dossier ;
- `/` : recherche globale, puis `Entrée` pour ouvrir le résultat et `Escape` pour annuler ;
- `a` : nouvelle entrée, ou nouveau dossier si le vault est encore vide ;
- `f` : nouveau dossier ; `Escape` : annuler un formulaire ; `F2` : afficher/masquer le mot de passe pendant sa saisie ;
- `r` : afficher/masquer le mot de passe sélectionné ; `l` : copier le login ; `p` : copier le mot de passe ;
- `d` : supprimer l'entrée sélectionnée ; `q` : quitter.

Les créations et suppressions sont sauvegardées de façon asynchrone. Une modal bloque les commandes pendant le chiffrement et l'écriture atomique.

## Protection des données

Le fichier est un format binaire v1. Le payload complet est chiffré avec XChaCha20-Poly1305, et la clé est dérivée du mot de passe maître avec Argon2id (profil libsodium `MODERATE`). Le sel, les paramètres KDF, le nonce et les métadonnées authentifiées font partie du format ; le mot de passe maître n'est pas conservé par l'application après la session.

Cette implémentation est un projet local en cours de développement et ne constitue pas un audit de sécurité indépendant. Ne versionnez jamais un fichier de vault ni un mot de passe dans ce dépôt.

## Développement assisté par IA

Une assistance d'IA générative a été utilisée pendant le développement, notamment pour proposer des commentaires et générer une partie des tests. Les changements ont été relus et validés dans le contexte du projet ; cette assistance ne remplace ni la revue humaine ni un audit de sécurité. Les détails sont dans [AI_USAGE.md](AI_USAGE.md).

## Licence

Aucune licence de redistribution n'est attribuée pour le moment. Tous les droits restent réservés tant qu'une licence n'a pas été ajoutée par l'auteur.
