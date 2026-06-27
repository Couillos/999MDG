# PRD V3 — PassivBot Alignment & TUI/Plot Improvements

## 1. Aligner les formules de metrics sur PassivBot

### Constat
Les formules actuelles dans `src/metrics/calculator.cpp` divergent de PassivBot :

| Metric | 999MDG actuel | PassivBot |
|--------|---------------|-----------|
| **gain_usd** | Somme des equity changes positifs tick-by-tick (valeur USD) | Ratio `end_equity / start_equity` avec EMA smoothing (alpha=0.5, span=3) |
| **sharpe_ratio_usd** | `(mean_ret / std_ret) * sqrt(252)` sur daily close returns | `adg / std_dev(daily_min_pct_changes)` (daily MIN, pas close) |
| **calmar_ratio_usd** | `cagr_clamped / drawdown_worst` (CAGR via simple return) | `adg / drawdown_worst` (ADG via smoothed gain) |
| **adg_usd** | `mean(daily_returns)` (arithmétique) | `gain^(1/n_days) - 1` (géométrique, via smoothed gain) |
| **mdg_usd** | `median(daily_returns)` | OK — même formule |

### Changements à faire

1. **Ajouter `smoothed_terminal_gain()`** dans `calculator.cpp` :
   - Prendre les daily equities (closing value of each day)
   - EMA smoothing avec alpha = 2/(span+1), span=3 → alpha=0.5
   - `gain = smoothed_end / smoothed_start` (ratio, pas USD)
   - Remplacer le calcul de `adg_usd` par `gain^(1/n_days) - 1`
   - Stocker `gain` dans `Metrics` (nouveau champ)

2. **Changer `sharpe_ratio_usd`** :
   - Utiliser `adg` (nouveau, géométrique) comme numérateur
   - Utiliser `stddev(daily_min_pct_changes)` comme dénominateur
   - `daily_min_pct_changes` = variation en % du min equity de chaque jour

3. **Changer `calmar_ratio_usd`** :
   - Numérateur = `adg` (nouveau, géométrique) au lieu de CAGR clampé

4. **Renommer `gain_usd` → `gain`** (ratio), garder l'ancien `gain_usd` comme `gain_dollar` pour display

### Fichiers impactés
- `src/metrics/types.h` — ajouter `gain` (ratio)
- `src/metrics/calculator.cpp` — réécrire `compute_metrics`
- `src/ui/tui.cpp` — mettre à jour `format_metric` et `metric_value`

---

## 2. Runtime Exposure Limit Enforcement

### Constat
`total_wallet_exposure` est utilisé UNIQUEMENT pour calculer `slot_capital` dans `entry_grid.cpp`. Il n'y a AUCUNE vérification runtime. En cas de:
- Price drop → double-down entries s'accumulent
- Price rally → la valeur des positions existantes augmente

L'exposition peut dépasser `balance * total_wallet_exposure` (ex: 900%).

### Changements à faire

1. **Ajouter wallet_exposure limit check dans le backtest loop** (`strategy.cpp` ou `entry_grid.cpp`) :
   - Avant chaque entry grid, calculer `wallet_exposure = total_qty * price / balance`
   - Si `wallet_exposure >= total_wallet_exposure / n_positions * 0.999`, refuser la nouvelle entry
   - Suivre le pattern PassivBot: `entries.rs` lignes 192-205

2. **Ajouter auto-reduce (enforce_exposure_limit)** :
   - Quand `wallet_exposure > limit * 1.01`, générer un ordre de close partiel
   - Réduire la position à `ideal_psize` (via interpolation linéaire)
   - Calculer `close_qty = current_size - ideal_psize`
   - Appliquer dans la boucle principale après les closes normales

3. **Ajouter `wallet_exposure_limit` dans les types** :
   - `wallet_exposure_limit = total_wallet_exposure / n_positions`

### Fichiers impactés
- `src/strategy/types.h` — ajouter wallet_exposure_limit (ou le calculer à la volée)
- `src/strategy/entry_grid.cpp` — ajouter le check avant entry
- `src/strategy/strategy.cpp` — ajouter l'auto-reduce dans la boucle principale

---

## 3. Metrics Overlay sur le Graphique Balance/Equity

### Constat
Le `equity_chart()` actuel (`plotter.cpp`) n'affiche que les courbes equity + balance, sans metrics.
Le script JK2 (`~/latin-fund/tools_suite/plot_balance_equity_jk2.py`) a un `create_metrics_panel()` qui affiche les metrics en overlay.

### Changements à faire

1. **Ajouter un label gnuplot avec les metrics** dans `equity_chart()` :
   - Utiliser `set label` avec les metrics clés: sharpe, calmar, gain, mdg, adg, drawdown, total return
   - Position: coin supérieur droit, fond semi-transparent (ou pas)
   - Texte monospace, format type HUD

2. Format suggéré :
```
┌──────────────────────────────┐
│ Sharpe:  1.2345    MDG: 0.05%│
│ Calmar:  0.9876    ADG: 0.12%│
│ Gain:    1.4567    DD:  -8.5%│
│ Return: +45.67%    CV:  0.000│
└──────────────────────────────┘
```

### Fichiers impactés
- `src/plot/plotter.cpp` — modifier `equity_chart()`
- `src/plot/colors.h` — si besoin de couleurs supplémentaires

---

## 4. Ajouter time_based_unstuck dans analysis.json

### Constat
Les paramètres `time_based_unstuck_pct`, `time_based_unstuck_threshold`, `time_based_unstuck_age` sont dans `btc.json` et utilisés dans le strategy loop, mais absents de `write_analysis_json()`.

### Changements à faire
Ajouter ces 3 paramètres dans la section `parameters` de `write_analysis_json()` dans `main.cpp`.

### Fichiers impactés
- `src/main.cpp` — ajouter les 3 champs manquants

---

## 5. TUI — Pareto Front Display (DÉJÀ FAIT)

### Changements
- Filtre rank=1 (Pareto front)
- Tri par crowding_distance desc
- Multi-lignes par candidat avec `param_abbrev()`
- Abréviations uniques pour les noms de paramètres (plus de `close_gr=0. close_gr=0.`)

---

## Récapitulatif des fichiers modifiés

| Fichier | Changements |
|---------|-------------|
| `src/metrics/types.h` | Ajout `gain` (ratio géométrique terminal lissé) |
| `src/metrics/calculator.cpp` | New `daily_min_equity()`, `smoothed_terminal_gain_and_adg()`; Changement formules adg_usd, sharpe_ratio_usd, calmar_ratio_usd, adg_smoothed |
| `src/strategy/entry_grid.cpp` | Ajout wallet_exposure limit check avant chaque entry |
| `src/strategy/strategy.cpp` | Ajout auto-reduce step (enforce exposure limit) |
| `src/plot/plotter.h` | Ajout paramètre `Metrics` au constructeur |
| `src/plot/plotter.cpp` | Ajout `fmt_metric()`, metrics overlay label dans `equity_chart()` |
| `src/ui/tui.cpp` | Pareto-only display, `param_abbrev()`, multi-lignes; ajout `gain` dans format_metric/set_metric_value |
| `src/optimizer/optimizer.cpp` | Ajout `gain` dans metric getter et JSON output |
| `src/main.cpp` | Ajout time_based_unstuck params dans analysis.json; passage metrics au Plotter |
| `tests/test_metrics.cpp` | Mise à jour test AdgSmoothed pour nouvelle formule EMA |
| `PRD_V3.md` | Ce fichier |

## Tests
- ✅ `test_metrics` — 9/9 passed
- ✅ `test_config` — 6/6 passed
- ✅ `test_strategy` — 4/4 passed
- Build complet sans warnings
