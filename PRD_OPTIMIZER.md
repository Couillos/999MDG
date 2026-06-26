# PRD v2 : Optimiseur NSGA-II aligné sur PassivBot (enarjord)

## 1. Problèmes Identifiés

L'implémentation actuelle dévie de PassivBot sur trois points critiques :

| Notre implémentation | PassivBot (v7.9+) | Problème |
|-|-|-|
| `scoring: {metric, weight}` avec signe du poids = direction | `scoring: {metric, goal}` avec `goal: "min"\|"max"` explicite | Le signe du poids est ambigu (confondu avec l'importance) |
| Objectives = z-scores normalisés | Objectives = valeurs brutes * `engine_sign` (-1 si max, +1 si min) | La normalisation z-score déforme l'espace de recherche du NSGA-II et n'est pas utilisée par PassivBot |
| `score` scalaire pondéré + `ok` booléen | Pas de score scalaire ; tri par Pareto rank + crowding distance ; `constraint_violation` continue | Le score et le booléen sont artificiels et trompeurs |

## 2. Solutions

### 2.1 Scoring explicite par goal

```json
// Nouveau format
{"metric": "sharpe_ratio_usd", "goal": "max", "weight": 1.0}
```

- `goal`: `"min"` ou `"max"` — direction d'optimisation explicite
- `weight`: importance relative (pour l'affichage uniquement)
- Rétrocompatibilité : si `goal` absent, on l'infère du signe de `weight` (négatif → max, positif → min). Si `weight` absent, weight=1.0.

### 2.2 Objectives brutes en engine-space

L'engine-space transforme toutes les métriques en problèmes de minimisation :

```
engine_sign = (goal == "max") ? -1.0 : 1.0
objective = raw_metric_value * engine_sign
```

- Pour une métrique à maximiser (sharpe) : `objective = -raw`. Minimiser -sharpe = maximiser sharpe ✓
- Pour une métrique à minimiser (drawdown) : `objective = +raw`. Minimiser raw = minimiser drawdown ✓
- Pas de z-score. Pas de normalisation.
- Le NSGA-II travaille directement sur ces valeurs engine-space.

### 2.3 Suppression du score scalaire et du booléen "ok"

- `score` supprimé de `Individual` et `RunResult`
- `ok`/`valid` supprimé
- Tri des résultats : Pareto rank (ascendant), puis crowding distance (descendante)
- Affichage TUI : solutions du front de Pareto (rank=1) avec leurs objectives engine-space

### 2.4 Contrainte violation continue

- `constraint_violation` = somme des dépassements de limites (inchangé, déjà correct)
- Pas de seuil arbitraire ε. La violation est utilisée directement dans la sélection par tournoi (déjà fait)
- Backtest échoué → `constraint_violation = 1e18` (comme PassivBot)

## 3. Métriques

### 3.1 `adg_smoothed`
ADG géométrique avec terminal lissé moyenne des 3 dernières equity quotidiennes.
- `end = mean(daily_eqs[-3:])`
- `adg_smoothed = (end / start)^(1/n_days) - 1`

### 3.2 `drawdown_worst_mean_1pct`
Moyenne des 1% pires drawdowns quotidiens.
- Pour chaque jour, drawdown = (peak - current) / peak
- Trier, prendre les 1% plus négatifs, moyenne des valeurs absolues.

### 3.3 `sterling_ratio`
`adg_smoothed / drawdown_worst_mean_1pct`

## 4. Fichiers modifiés

| Fichier | Changement |
|-|-|
| `src/config/types.h` | `ScoringMetric` : ajouter `goal`, `engine_sign` |
| `src/config/loader.cpp` | Parser `goal` (rétrocompatible avec `weight` seul) |
| `src/optimizer/nsga2.h` | Supprimer `score` de `Individual` |
| `src/optimizer/nsga2.cpp` | Supprimer `compute_weighted_score` (ou réécrire en `compute_display_score` pour TUI) |
| `src/optimizer/optimizer.h` | Ajuster `OptimizerResult` |
| `src/optimizer/optimizer.cpp` | `compute_objectives_for_population` : engine-space brut, pas de z-score, pas de score. `write_result_json`/`write_pareto_json` : supprimer `score`. `write_live_state` : trier par rank. |
| `src/optimizer/types.h` | Supprimer `score` de `RunResult` |
| `src/main.cpp` | Supprimer `score` de l'analyse JSON. Ajuster callback. |
| `src/ui/tui.cpp` | Trier par Pareto rank, pas par score. Supprimer colonne "ok". |
| `configs/btc.json` | Mettre à jour scoring avec `goal` + `weight` |

## 5. Spécification détaillée

### 5.1 ScoringMetric (config/types.h)

```cpp
struct ScoringMetric {
    std::string metric;
    double weight = 1.0;       // importance relative (affichage)
    std::string goal = "max";  // "min" ou "max"
    double engine_sign = -1.0; // dérivé : -1 pour max, +1 pour min
};
```

### 5.2 compute_objectives_for_population

```cpp
void compute_objectives_for_population(
    std::vector<Individual>& population,
    const std::vector<ScoringMetric>& scoring)
{
    size_t n_obj = scoring.size();
    for (auto& ind : population) {
        ind.objectives.resize(n_obj);
        for (size_t j = 0; j < n_obj; ++j) {
            double raw = get_metric_value(ind.metrics, scoring[j].metric);
            ind.objectives[j] = raw * scoring[j].engine_sign;
        }
    }
}
```

Plus de z-score, plus de weighted score. Les objectives sont en engine-space (toujours à minimiser).

### 5.3 Individual (nsga2.h)

```cpp
struct Individual {
    std::vector<double> genes;
    std::vector<double> objectives;         // engine-space (toujours minimiser)
    double constraint_violation = 0.0;      // somme des dépassements
    int rank = 0;
    double crowding_distance = 0.0;
    Config config;
    Metrics metrics;
    int generation = 0;
};
```

### 5.4 TUI (tui.cpp)

- Trier les solutions par Pareto rank ascendant, puis crowding distance descendant
- Afficher les objectives engine-space avec leur nom de métrique
- Supprimer les colonnes "score" et "ok"
- Mettre en évidence le front de Pareto

### 5.5 Output JSON

`results.json.zst` et `_pareto.json` :
- Supprimer le champ `score`
- Supprimer le champ `rank` et `crowding_distance` (interne)
- Conserver `params`, `objectives`, `constraint_violation`, `metrics`
- Ajouter `goals` dans le header : `["sharpe_ratio_usd:max", ...]`

## 6. Tests

- Vérifier que les objectives engine-space sont cohérentes : sharpe élevé → objective négatif, drawdown bas → objectif bas
- Vérifier que le front de Pareto contient des solutions non-dominées
- Vérifier la rétrocompatibilité du parsing de scoring (weight seul → inférer goal)
- Build et tests existants (test_config, test_strategy, test_metrics) passent
