# Recette manuelle TUI

Cette recette couvre les interactions FTXUI qui restent volontairement hors des tests Catch2.

## Préparation

```text
export VCPKG_ROOT=/Users/jack/vcpkg
cmake --build --preset vcpkg-debug -j2
./build-vcpkg/vault --vault /chemin/vers/mon.vault
```

Au premier lancement, le chemin doit être absent : saisir deux fois un mot de passe
maître non vide et identique. Le fichier est créé chiffré avant l’ouverture de FTXUI et
le vault vide doit afficher les trois colonnes sans erreur. Les lancements suivants ne
doivent demander le mot de passe qu’une fois. Aucun mot de passe ne doit être passé dans
la ligne de commande et aucune donnée de démonstration ne doit être ajoutée dans
`main.cpp`.

Pendant le chargement d’un vault existant ou la création du premier fichier, un spinner
terminal « Chargement/Création du vault sécurisé » doit rester visible sans afficher le
mot de passe. La durée correspond à Argon2id et ne doit pas être réduite pour accélérer
l’interface.

## Scénarios

1. Relancer avec le même chemin et le mauvais mot de passe : le démarrage doit s’arrêter sans remplacer le fichier ni afficher la TUI.
2. Appuyer sur `f` depuis n’importe quelle colonne, saisir un nom, puis `Entrée`. Vérifier que le dossier apparaît et que le compteur vaut zéro.
3. Appuyer une seconde fois sur `f` et tenter le même nom : la création doit être refusée sans modifier le vault. `Escape` annule un formulaire ouvert.
4. Sélectionner un dossier avec `↑`/`↓`, puis appuyer sur `Entrée` pour entrer dans le dossier. Les flèches `←` et `→` permettent aussi de changer de colonne.
5. Appuyer sur `a` dès qu’un dossier existe, saisir le titre, le login et le mot de passe avec `Tab`, puis valider par `Entrée`. Le mot de passe est masqué ; `F2` le révèle temporairement dans le formulaire.
6. Vérifier que l’entrée apparaît dans le dossier et que la sauvegarde est immédiate. Relancer l’application pour confirmer la persistance.
7. Ouvrir une entrée avec `Entrée` et vérifier l’identifiant et le login affichés.
8. Appuyer sur `r` et vérifier que le mot de passe passe de masqué à visible, puis revient à l’état masqué.
9. Appuyer sur `l` puis `p` et vérifier que le login puis le mot de passe sont copiés dans le presse-papiers. Si le backend système est indisponible, seule une erreur générale doit apparaître.
10. Appuyer sur `/`, saisir une partie du nom de dossier, de l’identifiant ou du login avec une casse différente, puis vérifier les résultats globaux.
11. Sélectionner un résultat de recherche, appuyer sur `Entrée`, puis vérifier que le dossier et l’entrée correspondants deviennent actifs.
12. Appuyer sur `d` sur une entrée sélectionnée : la suppression doit être immédiatement sauvegardée. Relancer l’application et vérifier que l’entrée supprimée reste absente.
13. Appuyer sur `/`, saisir du texte, puis `Escape` ; la recherche doit être annulée sans modifier le vault.
14. Appuyer sur `q` et vérifier le retour propre au terminal.
15. Pendant la création d’un dossier ou d’une entrée, vérifier que la modal carrée
    « Sauvegarde sécurisée » et son spinner apparaissent immédiatement. Les touches de
    navigation, de mutation et `q` restent ignorées jusqu’à la fin de l’écriture.
16. Simuler une erreur de sauvegarde (chemin non inscriptible ou parent indisponible) :
    le formulaire conserve ses valeurs, l’entrée/dossier n’est pas ajouté et le message
    apparaît en rouge.
17. Vérifier le thème noir/ambre : cadres carrés, sélection noire sur ambre, succès vert,
    erreur rouge et texte secondaire gris. Redimensionner le terminal sous 96 colonnes :
    les panneaux doivent s’empiler et les raccourcis du bas doivent se replier sans être
    coupés.
