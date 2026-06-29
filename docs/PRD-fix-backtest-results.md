# PRD — Correction des résultats d'optimisation/backtest (PowerMDG)

> **Statut** : à exécuter · **Auteur** : audit Claude (Opus) · **Date** : 2026-06-29
> **Cible** : `configs/btc.json` (grid/martingale long PassivBot-style sur BTCUSDT 1h, 2021→2025)
> **Symptôme** : `optimize --backtest-best` produit des configs qui détruisent le compte
> (10 000 $ → ~5 $, −99.95 %) tout en affichant des **métriques positives** au moment du scoring.

---

## 1. Contexte & symptômes observés

| Run (backtests/…) | final_equity | gain | drawdown_worst | Cohérent ? |
|---|---|---|---|---|
| 09-42-45 (btc.json par défaut, binaire actuel) | 9 787 $ | 0.9786 | 0.0441 | ✅ |
| 09-28-01 | 5 091 $ | 0.5090 | 0.5042 | ✅ |
| 09-22-15 | 804 $ | 0.0804 | 0.9260 | ✅ |
| **09-29-02 (best optimisé)** | **4.96 $** | **1.0571** | **0.0285** | ❌ |
| **09-28-45 (best optimisé)** | **30.32 $** | **1.0278** | **0.0035** | ❌ |

Fait dur, vérifié sur `backtests/2026-06-29_09-29-02/data/equity_curve.csv` :
la courbe d'équité descend **de façon monotone** de 10 000 $ à 4.96 $ (max=10 000, min=4.69,
**aucun NaN, aucune valeur négative**). Pourtant `drawdown_worst = 0.0285` (devrait être ≈ 0.9995)
et `gain = 1.057` (devrait être ≈ 0.0005). **Pour tous les runs « normaux », `gain == final/initial`
exactement ; le calcul ne se casse que sur les blow-ups.** Conséquence : l'optimiseur classe une
config qui ruine le compte comme « excellente » → toute optimisation converge vers des configs ruineuses.

Deux familles de causes coexistent :
1. **Logique de stratégie inversée/cassée** depuis le refactor en modules → les configs ruineuses *existent* dans l'espace de recherche.
2. **Métriques/plomberie d'optimisation incohérentes** → ces configs ruineuses sont *récompensées* au lieu d'être pénalisées.

Il faut corriger **les deux** : réparer la logique ET rendre l'objectif d'optimisation robuste (une stratégie qui perd 99 % ne doit JAMAIS scorer mieux qu'une qui perd 20 %).

---

## 2. Audit — problèmes identifiés (avec sévérité et localisation)

### CRITIQUE (C)

**C1 — Direction d'entrée inversée dans `ema_dist_pct`** · `src/strategy/modules/entry_condition/ema_dist_pct.cpp:9-10`
```cpp
double const threshold = ctx.ema * (1.0 + ctx.cfg.strategy.entry_ema_distance_pct);
return ctx.candle.close > threshold;   // entre LONG quand le prix est AU-DESSUS de l'EMA
```
Un grid long martingale doit entrer **sur repli** (`close < EMA*(1 − dist)`), pas au sommet.
Entrer haut puis moyenner à la baisse = chemin de ruine classique. `bb_reversion` et `zscore_ou`
entrent correctement sur repli ; seul le module par défaut est inversé.

**C2 — La condition d'entrée bloque aussi les double-downs** · `src/strategy/strategy.cpp:398-400`
```cpp
if (!entry_condition->should_enter(ctx)) continue;   // testé AVANT le branch first-entry/DD
```
Le martingale n'ajoute (`compute_entries`) que si `close < avg_entry` (prix en baisse), mais la
condition EMA exige `close > EMA*(1+dist)`. Les deux sont contradictoires dès que le prix passe
sous l'EMA : la grille ne peut quasi jamais moyenner sur le repli pour lequel elle est conçue.
La condition d'entrée doit gater **uniquement la première entrée**, pas les ajouts de grille.

**C3 — Le branch `atr_filter_mult` écrase 2 paramètres sans rapport** · `src/optimizer/optimizer.cpp:166-169`
```cpp
} else if (name == "atr_filter_mult") {
    cfg.strategy.atr_filter_mult = value;
    cfg.strategy.tp_min_upnl_pct = value;                       // BUG (merge raté)
    cfg.strategy.time_based_unstuck_age = static_cast<int>(value); // BUG
}
```
Tout génome touchant `atr_filter_mult` corrompt `tp_min_upnl_pct` et `time_based_unstuck_age`.

**C4 — Incohérence des métriques sur les courbes de blow-up** · `src/metrics/calculator.cpp`
Sur une courbe monotone 10 000 → 4.96, `drawdown_worst` (max_drawdown sur `daily_equity`, l.105-114/214)
renvoie 0.0285 et `smoothed_terminal_gain_and_adg` (l.155-175) renvoie gain 1.057. Mathématiquement
impossible avec la formule lue → soit `daily_equity`/le lissage a un bug d'index/d'échelle, soit le
binaire ayant produit ces analyses est différent du source actuel. **À reproduire et corriger en
priorité** (voir §5, boucle de debug). Tant que ce n'est pas garanti, l'optimiseur est aveugle.

### HAUTE (H)

**H1 — `tp1_fired` n'est jamais mis à `true`** · `src/strategy/types.h:20`, jamais assigné dans `strategy.cpp`
- `graduated_tp.cpp:48,53` : TP2/TP3 gated sur `tp1_fired` → **code mort**, la position n'est jamais soldée.
- `time_stop.cpp:9` : `if (pos.tp1_fired) return;` → le time-stop reste armé en permanence et clôture tout au bout de `time_stop_hours`.

**H2 — `graduated_tp` TP3 (trailing) a un corps vide** · `src/strategy/modules/closes_algo/graduated_tp.cpp:59-63`
Le bloc `if (atr > 0.0)` ne contient que des commentaires → aucun ordre. Le reliquat n'est jamais clôturé.

**H3 — `entry_side` n'est jamais renseigné** · `execute_first_entry` `src/strategy/strategy.cpp:73-83`
Reste à 0 ; `atr_stop.cpp:42` prend la branche long « par accident » et toute logique short est inaccessible.

**H4 — Paramètres de grille sans valeur par défaut → 0 silencieux** · `src/config/types.h:30-42`
`entry_ema_period, entry_grid_spacing_pct, initial_qty_pct, double_down_factor, close_grid_spacing_pct,
close_grid_count, sl_upnl_pct, n_positions, parkinson_volatility_span, maker_fee_pct,
time_based_unstuck_pct, time_based_unstuck_age` n'ont **aucun initialiseur**. Si un config les omet,
ils valent 0. `entry_grid_spacing_pct = 0` → division par zéro dans `martingale.cpp:38` /
`dca_linear.cpp:17` (`levels_filled` = inf/NaN → cast int indéfini). `time_based_unstuck_age = 0` →
`legacy_unstuck` se déclenche à chaque bougie.

**H5 — Les poids de scoring (`weight`) ne sont jamais appliqués** · `src/optimizer/optimizer.cpp:318`
`ind.objectives[j] = raw * scoring[j].engine_sign;` — `weight` est lu, écrit dans le live-state,
mais jamais utilisé. NSGA-II travaille en dominance de Pareto multi-objectifs : les `weight: 0.8/0.3`
de `btc.json` n'ont **aucun effet**. Soit appliquer le poids, soit documenter explicitement que NSGA-II
les ignore et fournir un mode agrégé scalarisé.

**H6 — Pas de garde-fou « ruine » dans l'objectif** · `src/metrics/calculator.cpp` + scoring
Une config qui finit à 0.05 % du capital peut dominer sur certains objectifs (Sharpe/Calmar calculés
sur des rendements quotidiens lissés) alors que `gain ≈ 0`. Il manque (a) une métrique de rendement
total fiable utilisée comme objectif/limite, et (b) une **limite dure** rejetant tout `gain < seuil`
(p.ex. `final_equity/initial < 0.5`) ou `drawdown_worst > 0.5` calculé correctement.

### MOYENNE (M)

**M1 — `warmup_candles` sous-dimensionné pour les génomes** · `src/optimizer/optimizer.cpp:276` + `loader.cpp:833`
Le warmup est figé sur la valeur du config de base (`max(ema_period, parkinson_span)` du *base*),
pas sur les **maxima des bounds**. Si la GA choisit `entry_ema_period`/`parkinson_span` > base,
l'indicateur lit un historique insuffisant. Sur `btc.json` ça passe de justesse (bornes ≤ base),
mais c'est un piège dès qu'on élargit les bounds.

**M2 — `legacy_unstuck` dimensionne sa tranche sur `initial_balance_usd`** · `legacy_unstuck.cpp:16`
`tranche = pct * initial_balance_usd` (fixe) puis clampée à `total_qty` → quand le compte fond,
elle solde la position entière d'un coup au lieu d'un dé-risque progressif.

**M3 — `parkinson_volatility_span` peut être optimisé à 1** · `configs/btc.json:80` `[1,24,1]` vs `loader.cpp` exige ≥ 2
Validé une seule fois au chargement du base ; `apply_param_to_cfg` ne revalide pas → span=1 évalué par la GA.

**M4 — `is_valid_bound_param` accepte des params de loss modules inconditionnellement** · `src/config/loader.cpp:254-258`
Une borne sur `atr_stop_mult` est acceptée/optimisée même sans module `atr_stop` actif → dimension de génome gaspillée.

**M5 — `simple_grid` peut laisser de la poussière** · `src/strategy/modules/closes_algo/simple_grid.cpp:45-47`
`round_step(remaining/count, step)` (arrondi au plus proche) peut sous-clôturer ; le reliquat au-delà du dernier niveau n'est jamais balayé.

### BASSE (L)

- **L1** `martingale`/`dca_linear` : première entrée silencieusement no-op si `qty < min_qty` (pas de bump min-qty) → stall permanent possible. `martingale.cpp:24-28`.
- **L2** `n_positions: [1,1]` consomme un axe de génome constant. `btc.json:79`.
- **L3** `configs/schema.json` obsolète (ne documente pas zscore_ou/bb_reversion/dca_linear/graduated_tp/mean_revert_tp/loss_algo ni le format `["tf",lo,hi,step]`).
- **L4** Le multiplicateur martingale utilise `factor^level` avec `level` démarrant à 1 (1er ajout déjà scalé par `factor`, pas `factor^0`) — à confirmer vs intention. `martingale.cpp:44-48`.

---

## 3. Spécification des correctifs (comportement cible)

| ID | Correctif attendu | Critère de validation |
|---|---|---|
| C1 | `ema_dist_pct` : entrer quand `close < ema*(1 − entry_ema_distance_pct)` (repli). Garder le sens « breakout » disponible via un flag si besoin, mais le défaut grid long = dip-buy. | Test unitaire : prix sous l'EMA → `should_enter==true` ; au-dessus → `false`. |
| C2 | `strategy.cpp` : n'appeler `entry_condition->should_enter` que pour la **première entrée**. Les double-downs sont gouvernés par l'`entries_algo` seul (espacement de grille). | Test : avec une position ouverte et prix en repli sous l'EMA, un double-down se déclenche. |
| C3 | Supprimer les 2 lignes parasites du branch `atr_filter_mult`. Chaque param a son propre branch. | Test : optimiser `atr_filter_mult` ne modifie ni `tp_min_upnl_pct` ni `time_based_unstuck_age`. |
| C4 | Garantir `gain ≈ final_equity/initial` (tol 1e-6) et `drawdown_worst ≈ (peak−trough)/peak` sur une courbe monotone décroissante. Corriger `daily_equity`/lissage/index si besoin. | **Test de régression** : courbe synthétique 10 000→5 → `gain<0.01`, `drawdown_worst>0.99`. |
| H1 | Mettre `pos.tp1_fired=true` dans `strategy.cpp` après l'exécution d'un close TP1 (ou faire porter l'état par le module). | Test : `graduated_tp` exécute TP2/TP3 ; `time_stop` se désarme après TP1. |
| H2 | Implémenter le trailing TP3 de `graduated_tp` (sortie du reliquat sur stop ATR) — pas de bloc vide. | Test : reliquat clôturé après TP3. |
| H3 | Renseigner `pos.entry_side` à l'entrée (long=1). | `atr_stop` utilise la bonne branche explicitement. |
| H4 | Donner des défauts sûrs **ou** faire échouer le chargement si un param requis par un module sélectionné est absent (validation stricte avec message clair). | Test : config sans `entry_grid_spacing_pct` → erreur explicite, pas de NaN. |
| H5 | Décider et implémenter : (a) appliquer `weight` aux objectifs, ou (b) mode scalarisé pondéré optionnel ; documenter le comportement NSGA-II. | Comportement documenté + test que le weight change l'ordre. |
| H6 | Ajouter une métrique de rendement total robuste (`total_return` / `final_equity_pct`) + **limite dure** `final_equity/initial >= 0.5` (configurable) et utiliser `drawdown_worst` corrigé comme limite. | Une config −99 % est rejetée (penalty/dominée) avant scoring. |
| M1 | `warmup_candles` = max des bornes hautes (`entry_ema_period.hi`, `parkinson_span.hi`, `zscore_vwap_lookback.hi`, `atr_period.hi`) en mode optimize. | Génome avec grand ema → assez de warmup. |
| M2 | Tranche unstuck proportionnelle à l'équité/position courante, pas à `initial_balance_usd`. | Dé-risque progressif vérifié. |
| M3 | Revalider les bornes après `apply_param_to_cfg` (clamp span≥2) ou interdire span<2 dans les bounds. | span=1 impossible. |
| M4 | `is_valid_bound_param` ne valide un param de loss module que si le module correspondant est sélectionné. | Borne `atr_stop_mult` sans `atr_stop` → erreur. |
| M5 | Dernier niveau de `simple_grid` clôture le reliquat exact (floor + sweep final). | Pas de dust résiduelle. |
| L1-L4 | Voir §2 ; correctifs mineurs + mise à jour `schema.json` + tests. | — |

---

## 4. Orchestration des sous-agents (Sonnet)

> **Principe** : vagues séquentielles avec barrière. À l'intérieur d'une vague, les agents tournent
> en parallèle sur des fichiers disjoints. Après chaque vague de correctifs, une vague de **revue**
> (agent reviewer indépendant) audite le diff vs ce PRD, puis une **boucle build+backtest** valide.
> Lancer tous les agents d'une même vague dans **un seul message** (tool calls multiples).

### Règles communes à tous les agents
- Modèle : **Sonnet**. Lire ce PRD (`docs/PRD-fix-backtest-results.md`) en entier avant de coder.
- Ne modifier que les fichiers assignés. Respecter le style C++23 existant (clang, namespaces, `const`).
- Chaque correctif = 1 test (unitaire dans `tests/` ou assertion d'invariant) prouvant le fix.
- Pas de changement de comportement non listé. Retourner un résumé : fichiers touchés, diff résumé, tests ajoutés, résultat `ctest`.
- **Ne pas** committer ; laisser le working tree modifié pour la revue.

### Vague 0 — Reproduction & filet de sécurité (1 agent, bloquant)
> But : verrouiller les invariants AVANT de toucher au code, pour mesurer le progrès.
- Agent **`repro`** :
  1. Builder le projet (`conan install . --output-folder=build --build=missing` puis cmake/clang Release, `BUILD_TESTING=ON`).
  2. Reproduire un blow-up avec le binaire **actuel** : créer `configs/_repro_blowup.json` à partir des params du best ruineux (ema 11, dist 0.0023, grid 0.0016, dd 1.41, sl −0.6294, close grid 0.0123×4, unstuck 0.221/102). Lancer `backtest`, lire `analysis.json`.
  3. Confirmer ou infirmer **C4** : `gain` vs `final/initial`, `drawdown_worst` vs courbe. Si le binaire actuel est cohérent, documenter que les analyses fautives venaient d'un binaire périmé ; sinon localiser le bug exact dans `calculator.cpp`.
  4. Écrire `tests/test_metrics_invariants.cpp` : courbes synthétiques (monotone décroissante, monotone croissante, en cloche) → asserts sur `gain`, `drawdown_worst`, cohérence Sharpe/Calmar signe.
  5. Livrable : rapport de repro + test rouge/vert + diagnostic C4 précis.

### Vague 1 — Correctifs logique stratégie (3 agents //, fichiers disjoints)
- Agent **`fix-entry`** → **C1, C2** : `ema_dist_pct.cpp/.h`, `strategy.cpp` (déplacer le gate `should_enter` dans la seule branche first-entry). Tests dans `test_strategy.cpp`.
- Agent **`fix-closes`** → **H1, H2, M5** : `strategy.cpp` (set `tp1_fired`), `graduated_tp.cpp`, `simple_grid.cpp`. Tests dans `test_strategy.cpp`.
- Agent **`fix-position-state`** → **H3, M2** : `strategy.cpp` (`entry_side`), `legacy_unstuck.cpp`. Tests.
> Coordination : tous touchent `strategy.cpp` → **sérialiser** ces 3 sur `strategy.cpp` via worktrees isolés
> (`isolation: worktree`) puis merge par l'orchestrateur, OU regrouper les éditions de `strategy.cpp` dans un
> seul agent `fix-strategy-core` et laisser les deux autres sur les modules. **Recommandé** : 1 agent
> `fix-strategy-core` (C2,H1,H3 dans strategy.cpp) + 1 agent `fix-modules` (C1,H2,M2,M5 dans les modules).

### Vague 2 — Correctifs optimiseur / config / métriques (2 agents //)
- Agent **`fix-optimizer`** → **C3, H5, M1, M3, M4** : `optimizer.cpp`, `nsga2.cpp` si besoin. Tests dans `tests/`.
- Agent **`fix-config-metrics`** → **C4 (si non résolu en V0), H4, H6, L3** : `loader.cpp`, `types.h`, `calculator.cpp`, `metrics/types.h`, `schema.json`. Tests `test_config.cpp` + `test_metrics_invariants.cpp`.

### Vague 3 — Revue indépendante vs PRD (2 agents reviewer //)
- Agent **`review-logic`** : audite le diff des vagues 1 contre §3 (C1,C2,H1,H2,H3,M2,M5). Vérifie chaque critère de validation, relit les tests, cherche les régressions. Verdict par ID : ✅/❌ + preuve.
- Agent **`review-opt-metrics`** : idem pour §3 (C3,C4,H4,H5,H6,M1,M3,M4,L3).
> Les reviewers **ne corrigent pas** ; ils renvoient une liste de défauts. Toute case ❌ relance un agent fix ciblé (boucle).

### Vague 4 — Boucle de validation opti+backtest (1 agent pilote, voir §5)
- Agent **`validate-loop`** : exécute le protocole §5 jusqu'aux critères d'acceptation §6, ou rapporte le blocage précis.

---

## 5. Protocole de boucle debug (build → backtest → optimize → valider)

À exécuter par l'agent `validate-loop` (et reproductible à la main) :

```bash
# 1. Build
conan install . --output-folder=build --build=missing
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DBUILD_TESTING=ON
cmake --build build -j$(nproc)

# 2. Tests unitaires (doivent tous passer)
cd build && ctest --output-on-failure ; cd ..

# 3. Backtests de bon sens (invariants AVANT optimisation)
./build/src/powermdg backtest configs/btc.json            # défaut: doit rester cohérent
./build/src/powermdg backtest configs/_repro_blowup.json  # blow-up: gain≈final/init, dd≈réel
```

**Invariants vérifiés à chaque itération** (script ou agent qui parse `analysis.json`) :
- `abs(metrics.gain − final_equity/initial_balance) < 1e-4`.
- `metrics.drawdown_worst ∈ [0,1]` et cohérent avec `min(equity)/peak`.
- Aucune valeur `NaN`/`Inf`/négative dans `data/equity_curve.csv`.
- `final_equity > 0` ; si une config perd >99 %, ses objectifs doivent être **dominés** (ou penalty>0).
- `drawdown_worst` (best) ≤ limite config ; sinon le best ne devrait pas être sélectionné.

```bash
# 4. Mini-optimisation rapide (itération courte pour debug)
#    Cloner btc.json -> configs/_opt_smoke.json avec ga.population_size=64, n_generations=10
./build/src/powermdg optimize configs/_opt_smoke.json --backtest-best
#    -> lire results/optimize/<ts>/ et best/analysis.json : le best NE DOIT PAS être un blow-up.
```

**Critère de la boucle** : tant qu'un best optimisé a `final_equity/initial < 0.5` ou des métriques
incohérentes, l'orchestrateur identifie l'ID de bug responsable, relance l'agent fix correspondant,
rebuild, re-teste. Répéter jusqu'aux critères §6.

```bash
# 5. Optimisation complète (validation finale, une fois la smoke OK)
./build/src/powermdg optimize configs/btc.json --backtest-best
```

---

## 6. Critères d'acceptation (Definition of Done)

1. **Tests** : `ctest` 100 % vert, incluant `test_metrics_invariants` et les nouveaux tests stratégie/config/optimiseur.
2. **Cohérence métriques** : sur 3 courbes synthétiques + 2 backtests réels, `gain == final/initial` (tol 1e-4) et `drawdown_worst` cohérent. **C4 fermé.**
3. **Pas de récompense du blow-up** : sur une mini-opti (smoke), AUCUN best avec `final_equity/initial < 0.5`. Une config −99 % est dominée/pénalisée.
4. **Logique d'entrée** : `ema_dist_pct` entre sur repli ; double-downs non bloqués par la condition d'entrée (tests C1/C2 verts).
5. **Closes** : `graduated_tp` solde réellement la position (TP1→TP2→TP3) ; `simple_grid` ne laisse pas de dust ; `time_stop` se désarme après TP1.
6. **Plomberie opti** : `atr_filter_mult` n'écrase plus d'autres params ; `warmup` couvre les bornes ; poids de scoring soit appliqués soit explicitement documentés.
7. **Résultat métier** : sur `optimize configs/btc.json --backtest-best`, le best a un `total_return` positif **et** un `drawdown_worst` ≤ limite config, avec une courbe d'équité plausible (pas de blow-up caché). À défaut d'être rentable (le marché peut ne pas le permettre avec ces bounds), le best doit au minimum être **le moins mauvais réel** et non un artefact de métrique.
8. **Revue** : toutes les cases des vagues 3 en ✅, avec preuve par ID.

---

## 7. Annexe — fichiers clés

| Zone | Fichiers |
|---|---|
| Moteur backtest | `src/strategy/strategy.cpp`, `src/strategy/types.h`, `src/strategy/modules/module_context.h` |
| Entrées | `entry_condition/{ema_dist_pct,zscore_ou,bb_reversion}.cpp`, `entries_algo/{martingale,dca_linear}.cpp` |
| Sorties | `closes_algo/{simple_grid,graduated_tp,mean_revert_tp}.cpp` |
| Pertes | `loss_modules/{legacy_stop_loss,legacy_unstuck,z_stop,atr_stop,time_stop}.cpp` |
| Métriques | `src/metrics/calculator.cpp`, `src/metrics/types.h` |
| Optimiseur | `src/optimizer/{optimizer,nsga2}.cpp` |
| Config | `src/config/{loader,types.h}`, `configs/{btc.json,schema.json,template.json}` |
| Tests | `tests/test_{strategy,metrics,config,constraint_sort}.cpp` |

**Ordre de priorité de correction** : C4 → C3 → C1/C2 → H6 → H1/H4 → reste.
(D'abord rendre l'objectif fiable et non-trompeur, puis réparer la logique, puis affiner.)
