# PRD — Moteur de Backtest & Optimisation (C++ Ultra-Performant)

## 1. Objectif

Construire un moteur de backtest et d'optimisation de stratégies de trading 100 % en C++, capable de traiter des volumes de données historiques massifs avec une latence minimale. Le moteur doit permettre :

- **Backtest unitaire** : exécuter une stratégie sur une période donnée et produire un jeu complet de métriques.
- **Optimisation** : explorer un espace de paramètres défini par des bornes, avec des contraintes (`limits`) et un score multi-métriques, afin de trouver la meilleure configuration.
- Mise en cache des données de bougies pour éviter de re-télécharger et re-agréger à chaque exécution.

---

## 2. Fichier de configuration JSON

Un seul fichier `.json` sert à la fois pour le backtest et pour l'optimisation.

```jsonc
{
  "symbols": ["BTCUSDT", "ETHUSDT", "SOLUSDT", "DOGEUSDT"],
  "timeframe": "1h",                // 1m,5m,15m,30m,1h,4h,12h,1d
  "date_from": "2024-01-01",
  "date_to": "2024-12-31",
  "initial_balance_usd": 10000.0,
  "total_wallet_exposure": 2.0,

  "strategy": {
    "entry_ema_period": 200,
    "entry_ema_distance_pct": 0.02,
    "entry_grid_spacing_pct": 0.03,
    "initial_qty_pct": 0.03,
    "double_down_factor": 0.5,
    "close_grid_spacing_pct": 0.02,
    "close_grid_count": 3,
    "sl_upnl_pct": -0.05,
    "n_positions": 2,
    "parkinson_volatility_span": 24,
    "maker_fee_pct": 0.001
  },

  "optimize": {                     // ignoré en mode backtest
    "n_workers": 8,                 // threads workers (0 = hardware_concurrency)
    "limits": {
      "mdg_usd": { "min": 0.001 },
      "equity_balance_diff_neg_max_usd": { "max": 0.25 }
    },
    "scoring": [
      { "metric": "sharpe_ratio_usd", "weight": 1.0 },
      { "metric": "sortino_ratio_usd", "weight": 1.0 },
      { "metric": "calmar_ratio_usd",  "weight": 0.5 }
    ],
    "bounds": {
      "entry_ema_distance_pct": [0.01, 0.10],
      "entry_ema_period": [50, 400],
      "entry_grid_spacing_pct": [0.005, 0.10],
      "initial_qty_pct": [0.005, 0.25],
      "double_down_factor": [0.1, 2.0],
      "close_grid_spacing_pct": [0.005, 0.10],
      "close_grid_count": [1, 10],
      "sl_upnl_pct": [-0.20, -0.01],
      "n_positions": [1, 5],
      "parkinson_volatility_span": [12, 96],
      "total_wallet_exposure": [1.0, 5.0]
    }
  },

  "output": {
    "dir": "results"                      // dossier racine des sorties
  }
}
```

### Mode d'exécution

Le mode est déterminé par le premier argument CLI, pas par le JSON :

```
./martingale backtest  config.json    # lit strategy.params (fixes)
./martingale optimize config.json    # lit optimize.bounds (variables)
```

- En mode `backtest`, la section `optimize` est ignorée.
- En mode `optimize`, seuls les paramètres listés dans `optimize.bounds` sont variables ; les autres champs de `strategy` restent fixes (servent de valeur par défaut si absents des bounds).
- Tous les pourcentages sont en décimal (`0.02` = 2 %, jamais `2`).
- `total_wallet_exposure` : multiplicateur d'exposition totale du wallet (remplace `max_leverage`). Ex: `2.0` = on peut utiliser jusqu'à 2× le capital.
- `n_positions` : nombre maximum de positions simultanées (tous symboles confondus).
- `initial_qty_pct` : fraction du capital alloué par slot utilisée pour la première entry (ex: `0.03` = 3 %). La taille de chaque entrée successive = `previous_entry_size * double_down_factor`.
- `double_down_factor` : ratio de taille entre niveaux d'entry successifs. Si `double_down_factor < 1`, la série converge et le nombre d'entries est illimité (s'arrête quand `entry_size < min_qty`). Si `double_down_factor >= 1`, la série diverge et le capital du slot est épuisé après `n` entries.
- `maker_fee_pct` : frais maker appliqués à chaque ordre exécuté (ex: 0.001 = 0.1 %).

### Warmup déduit

Le moteur calcule automatiquement le nombre de bougies de warmup nécessaires :
```
warmup_candles = max(entry_ema_period, parkinson_volatility_span)
```
La date de début effective du chargement des données est reculée de `warmup_candles * timeframe`. Ces bougies sont utilisées pour initialiser les indicateurs (EMA, volatilité) mais aucune position n'est ouverte avant `date_from`.

---

## 3. Architecture du projet

```
martingale/
├── CMakeLists.txt
├── config/
│   └── schema.json              # JSON Schema de validation
├── src/
│   ├── main.cpp                 # Point d'entrée : lit le JSON, dispatch
│   ├── config/
│   │   ├── loader.h / .cpp      # Parse & valide le JSON (simdjson)
│   │   └── types.h              # Structs Config, StrategyParams, OptimizeBounds…
│   ├── data/
│   │   ├── candle.h / .cpp      # Struct Candle (time, o, h, l, c, v)
│   │   ├── candle_manager.h/.cpp# Télécharge, agrège, stocke, hash & cache
│   │   ├── binance_client.h/.cpp# HTTP(S) client léger (libcurl)
│   │   ├── symbol_info.h / .cpp # Filtres Binance (step_size, min_qty, …)
│   │   └── cache.h / .cpp       # Gestion du cache disque (fichiers .bin)
│   ├── strategy/
│   │   ├── strategy.h / .cpp    # Boucle de backtest, logique de trading
│   │   └── types.h              # Struct Position, Order, EquityPoint
│   ├── metrics/
│   │   ├── calculator.h / .cpp  # Calcule toutes les métriques de sortie
│   │   └── types.h              # Struct Metrics (tous les champs)
│   ├── optimizer/
│   │   ├── optimizer.h / .cpp   # Boucle d'optimisation (Grid Search)
│   │   └── types.h              # Struct OptConfig, Score, Limit
│   └── utils/
│       ├── hash.h / .cpp        # xxHash3 pour le cache
│       ├── time.h / .cpp        # Conversion dates, timestamps
│       ├── math.h / .cpp        # Utilitaires stats (moyenne, écart-type…)
│       └── thread_pool.h / .cpp # Pool de threads configurable (work-stealing)
├── data/
│   ├── candles/                 # Fichiers journaliers compressés (zstd)
│   │   ├── INDEX.bin            # Index global des fichiers disponibles
│   │   └── BTCUSDT/
│   │       ├── 2024-01-01.bin.zst
│   │       └── …
│   └── cache/                   # Fichiers .cache (mmap, non compressés)
│       └── <hash>.cache
├── src/
│   └── plot/
│       ├── plotter.h / .cpp     # Génération des graphiques PNG (via gnuplot pipe)
│       └── colors.h             # Palette de couleurs par symbole
├── tests/
│   ├── CMakeLists.txt
│   ├── test_config.cpp
│   ├── test_candle_manager.cpp
│   ├── test_strategy.cpp
│   ├── test_metrics.cpp
│   ├── test_optimizer.cpp
│   └── test_plotter.cpp
├── audit/
│   ├── test_no_lookahead.sh     # Script d'audit de non-look-ahead bias
│   └── generate_lookahead_test_data.py  # Génération de données synthétiques avec delays
└── scripts/
    ├── run_benchmark.sh
    └── install_gnuplot.sh       # Vérification/installation de gnuplot
```

---

## 4. Modules détaillés

### 4.1 Config Loader (`src/config/`)

- Utilise **simdjson** (parser JSON le plus rapide en C++).
- Valide le fichier contre un JSON Schema (`config/schema.json`) via une librairie légère ou une validation manuelle des champs.
- Produit une struct `Config` read-only.
- Calcule `warmup_start` à partir de `entry_ema_period` et `parkinson_volatility_span`.

### 4.2 Data Layer (`src/data/`)

#### Candle (format mémoire, utilisé par la stratégie)
```cpp
struct alignas(64) Candle {     // Une cache line par candle
  int64_t timestamp;            // Unix ms
  double open, high, low, close, volume;
};
```

#### CandleManager
- **Téléchargement** :
  - Requêtes HTTP GET à l'API REST Binance `https://api.binance.com/api/v3/klines`.
  - Télécharge uniquement les bougies `1m` (granularité native la plus fine).
  - Pour les timeframes supérieurs, agrège depuis les `1m` (OHLC : open = premier, high = max, low = min, close = dernier, volume = somme).
  - Parallélisation par symbole avec `std::async` ou thread pool.
- **Stockage journalier compressé** (format binaire `.bin.zst`) :
  - Chaque symbole a son dossier `data/candles/<SYM>/`.
  - Fichiers journaliers : `2024-01-01.bin.zst` (binaire, pas CSV).
  - Format binaire sur disque (après décompression) :
    ```
    [nb_candles: uint32]
    [timestamps: int64_t[nb_candles]]
    [opens:     double[nb_candles]]
    [highs:     double[nb_candles]]
    [lows:      double[nb_candles]]
    [closes:    double[nb_candles]]
    [volumes:   double[nb_candles]]
    ```
    Stockage columnar (tous les timestamps ensemble, tous les opens ensemble, etc.) pour une meilleure compressibilité via zstd.
  - Compression **zstd niveau 3** (bon ratio / vitesse de décompression).
  - Si le fichier existe déjà, ne pas re-télécharger (sauf si `--force`).
  - Un index global `data/candles/INDEX.bin` (non compressé) contient pour chaque symbole la liste des fichiers journaliers disponibles et leur taille en octets, permettant des lectures partielles sans scanner les dossiers.
- **Décompression à la volée** :
  - Quand le `CandleManager` charge une période, il lit chaque fichier journalier depuis le disque.
  - Chaque fichier `.bin.zst` est décompressé via `ZSTD_decompress()` en un buffer mémoire.
  - Les données sont converties en `std::vector<Candle>` (format struct-of-arrays → array-of-structs pour la stratégie).
  - Les buffers décompressés sont **jetés** après construction du cache (ne pas les garder en mémoire).
- **Warmup** :
  - Le `CandleManager` recoit `date_from` et `warmup_candles` (calculé à partir du plus grand des paramètres `entry_ema_period` et `parkinson_volatility_span`).
  - Il recule `date_from` de `warmup_candles * timeframe` pour déterminer `data_start`.
  - Les données sont chargées de `data_start` à `date_to`.
  - Une fois chargées, les candles sont découpées en deux spans : `warmup_span` (de `data_start` à `date_from`) et `trading_span` (de `date_from` à `date_to`).
  - Seul le `trading_span` est utilisé pour les métriques finales.
- **Cache** (format binaire non compressé, optimisé pour la vitesse de chargement) :
  - À la première utilisation, génère un **hash** (xxHash3) de la chaîne canonique :
    `"BTCUSDT,ETHUSDT|1h|2024-01-01|2024-12-31|warmup=200"`
  - Le `warmup` dans le hash est le nombre de bougies de warmup nécessaires. Si les paramètres changent (ex: EMA 200 → 100), le hash change → nouveau cache.
  - Construit un fichier binaire **non compressé** `data/cache/<hash>.cache` contenant un tableau contigu de `Candle` (sérialisation `memcpy`). Le fichier inclut un header de 32 octets : `magic (4B) | version (4B) | count (8B) | trading_start_index (8B) | flags (8B)`.
  - Pas de compression sur le cache : le fichier est conçu pour être `mmap` directement en mémoire — zéro parsing, zéro copie, zéro décompression.
  - Si le fichier cache existe, le `mmap` directement — les threads de l'optimiseur partagent les mêmes pages physiques via le cache kernel.
  - Hash invalide si les fichiers `.bin.zst` sources sont plus récents que le cache (comparaison `mtime`).

#### BinanceClient
- Client HTTP minimal : utilise `libcurl` avec `multi` handle pour multiplexer les requêtes.
- Limité à 1200 req/min (respect du rate limit).
- Découpage automatique des ranges de dates trop larges.
- Limité aux paires `/USDT:USDT` sur Binance spot.
- Récupère également `GET /api/v3/exchangeInfo` pour obtenir les `SymbolInfo` de chaque paire
  (lot_size: step_size, min_qty, min_notional ; price_filter: tick_size). Ces données sont
  mises en cache dans `data/candles/SYMBOLS.bin` et rafraîchies toutes les 24h.

### 4.3 Strategy — Logique de trading stateless avec double-down (`src/strategy/`)

#### Paramètres de la stratégie

```
entry_ema_period              – période de l'EMA (ex: 200)
entry_ema_distance_pct        – distance au-dessus de l'EMA pour autoriser les entries (ex: 0.02 = 2%)
entry_grid_spacing_pct        – espacement entre chaque niveau d'entry (ex: 0.03 = 3%)
initial_qty_pct               – fraction du capital du slot utilisée par la 1re entry (ex: 0.03 = 3 %)
double_down_factor            – ratio de taille entre niveaux d'entry successifs (ex: 0.5)
close_grid_spacing_pct        – espacement entre chaque niveau de close (ex: 0.02 = 2%)
close_grid_count              – nombre de niveaux de close
sl_upnl_pct                   – seuil de UPnL relatif pour le stop loss global (ex: -0.05 = -5%)
n_positions                   – nombre maximum de positions simultanées (tous symboles)
parkinson_volatility_span     – nombre de bougies pour la volatilité de Parkinson
maker_fee_pct                 – frais maker appliqués à chaque ordre (ex: 0.001 = 0.1 %)
```

#### État d'une position (stateless, déduit du marché)

Une position est définie uniquement par son prix d'entrée moyen et sa taille cumulée. Il n'y a pas de notion de "niveau" pré-défini :

```cpp
struct Position {
  double avg_entry_price;   // Prix d'entrée moyen pondéré
  double total_qty;         // Quantité totale détenue
  double traded_qty;        // Quantité déjà close (pour calcul du PnL réalisé)
  double realized_pnl;      // PnL réalisé (après closes partielles)
};
```

La stratégie ne mémorise pas quels niveaux de la grille ont été atteints : elle compare à chaque bougie le prix actuel et la taille pour décider de la prochaine action.

#### Logique de trading

À chaque nouvelle bougie (sur le `trading_span`) après warmup :

```
ÉTAPE 0 — Filtre multi-coin par volatilité
  Pour chaque symbole, calculer σ_parkinson sur parkinson_volatility_span :
    σ² = (1 / (4 * N * ln(2))) * Σ ln(high_i / low_i)²
  Trier par σ décroissant. Les min(n_positions, nb_symboles) premiers sont "actifs".
  Les autres symboles ne peuvent pas ouvrir de nouvelles positions, mais leurs
  positions existantes continuent d'être gérées (closes et SL).

ÉTAPE 1 — Closes partielles (tous les symboles avec position ouverte)
  Pour chaque symbole avec position ouverte (qty > 0) :
    avg  = position.avg_entry_price
    upnl = (close - avg) / avg         # UPnL relatif

    Pour k = 1 à close_grid_count :
      target_price = avg * (1 + k * close_grid_spacing_pct)
      if close >= target_price ET upnl >= k * close_grid_spacing_pct :
        # Close partielle : chaque niveau ferme 1/close_grid_count de la qty initiale
        close_qty = round(position.total_qty / close_grid_count, symbol_step_size)
        close_qty = min(close_qty, position.total_qty)
        Execute MARKET SELL close_qty @ close
        PnL réalisé += close_qty * (close - avg)
        position.total_qty -= close_qty
        position.traded_qty  += close_qty
        position.realized_pnl += close_qty * (close - avg) - fees

  Note : si UPnP relatif dépasse plusieurs niveaux dans la même bougie,
  seule la fraction correspondant au nombre de niveaux franchis est close.

ÉTAPE 2 — Stop loss (tous les symboles)
  Pour chaque symbole avec position ouverte :
    weighted_upnl = (close - position.avg_entry_price) / position.avg_entry_price
    if weighted_upnl <= sl_upnl_pct :
      Close tout (MARKET) : PnL = total_qty * (close - avg) - fees
      position → 0

ÉTAPE 3 — Entrées (symboles actifs uniquement)
  Pour chaque symbole actif (pas de position OU position existante) :
    Si nb positions total >= n_positions → skip

    Condition d'entrée : close > EMA(close, entry_ema_period) * (1 + entry_ema_distance_pct)
    Si non validée → skip

    # Capital alloué à ce slot
    slot_capital_usd = (total_balance * total_wallet_exposure) / n_positions

    Cas A — Pas de position existante (première entry) :
      entry_size_usd = slot_capital_usd * initial_qty_pct
      qty = round(entry_size_usd / close, symbol_step_size)
      entry_price = close   # market order
      Ouvrir LONG : avg_entry_price = entry_price, total_qty = qty
      entry_levels = 1      # nombre de niveaux déjà remplis

    Cas B — Position existante (double-down) :
      # Le nombre de niveaux d'entry que le prix actuel justifie
      price_drop_pct = (avg_entry_price - close) / avg_entry_price
      levels_filled = floor(price_drop_pct / entry_grid_spacing_pct)

      if levels_filled >= entry_levels :
        Pour l = entry_levels à levels_filled :
          # Taille = initial_qty_pct * slot_capital * double_down_factor^l
          qty_multiplier = double_down_factor^l     # 0.5^1, 0.5^2, …
          entry_size_usd = slot_capital_usd * initial_qty_pct * qty_multiplier
          qty = round(entry_size_usd / close, symbol_step_size)
          if qty < min_qty : break
          if entry_size_usd > (slot_capital_usd - total_entry_cost_usd) : break
          # Mise à jour du prix moyen pondéré
          avg_entry_price = (avg_entry_price * total_qty + close * qty) / (total_qty + qty)
          total_qty += qty
          entry_levels += 1
```

#### Calcul des ordres (step size, min, max)

Pour chaque symbole, lors de l'initialisation du backtest, le `CandleManager` récupère les filtres
de l'API Binance `GET /api/v3/exchangeInfo` :

```cpp
struct SymbolInfo {
  std::string symbol;       // "BTCUSDT"
  double      min_qty;      // taille minimale
  double      step_size;    // pas de quantité
  double      min_notional; // valeur minimale d'ordre (en USDT)
  int         price_decimals; // pour arrondi des prix
};
```

Toute quantité d'ordre est arrondie selon la règle `round(qty / step_size) * step_size`.
Si la quantité résultante est < `min_qty`, l'ordre n'est pas placé.

#### Fees

Tous les ordres exécutés (entries et closes) se voient appliquer `maker_fee_pct` :
```
fee = abs(order_value) * maker_fee_pct
```
Le `maker_fee_pct` est paramétrable dans le JSON (ex: `0.001` = 0.1 %).
Pour simplifier, on considère que tous les ordres sont des makers (stratégie à limite).

#### Volatilité de Parkinson

Formule utilisée (estimation de la volatilité sans biais de close-to-close) :

```
σ_parkinson = sqrt( (1 / (4 * N * ln(2))) * Σ_{i=1}^{N} ln(high_i / low_i)² )
```

Où `N = parkinson_volatility_span`. Calculée séparément pour chaque symbole à chaque bougie sur une fenêtre glissante.

#### Règles d'exécution

- La boucle est **vectorisée** : tous les symboles sont traités dans une même itération.
- Pas d'allocation dynamique dans la boucle chaude ; tout est pré-alloué (`std::vector` réservé).
- `EMA` implémentée de façon itérative : `ema = alpha * close + (1 - alpha) * ema_prev`.
- Le tableau des volatilités de Parkinson est mis à jour par fenêtre glissante (`O(1)` par bougie via deque circulaire de ln(high/low)²).
- Les calculs d'arrondi (`step_size`, `min_qty`) utilisent des lookup tables pré-calculées pour éviter les `fmod`/`floor` coûteux dans la boucle chaude.

### 4.4 Metrics (`src/metrics/`)

Le module reçoit une `std::span<EquityPoint>` (timestamp, equity, balance, positions…) et calcule :

| Métrique | Description |
|---|---|
| `adg_usd` | Average Daily Gain |
| `adg_per_exponential_fit_error_usd` | ADG / fit error |
| `adg_per_exposure_long_usd` | ADG / exposure long avg |
| `adg_per_exposure_short_usd` | ADG / exposure short avg |
| `calmar_ratio_usd` | CAGR / Max Drawdown |
| `entry_initial_balance_pct_long` | % balance initiale engagée en long |
| `entry_initial_balance_pct_short` | % balance initiale engagée en short |
| `equity_balance_diff_neg_max_usd` | Max négatif de (equity - balance) |
| `equity_balance_diff_neg_mean_usd` | Moyenne des écarts négatifs |
| `equity_balance_diff_pos_max_usd` | Max positif de (equity - balance) |
| `equity_balance_diff_pos_mean_usd` | Moyenne des écarts positifs |
| `equity_choppiness_usd` | Choppiness index de la courbe equity |
| `equity_jerkiness_usd` | Jerkiness (dérivée 3e) de la courbe equity |
| `expected_shortfall_1pct_usd` | CVaR à 1 % |
| `exponential_fit_error_usd` | Erreur RMS du fit exponentiel |
| `gain_usd` | Gain total |
| `gain_per_exposure_long_usd` | Gain / exposition longue moyenne |
| `gain_per_exposure_short_usd` | Gain / exposition short moyenne |
| `loss_profit_ratio` | Ratio pertes / profits globaux |
| `loss_profit_ratio_long` | Idem long uniquement |
| `loss_profit_ratio_short` | Idem short uniquement |
| `mdg_usd` | Median Daily Gain |
| `mdg_per_exponential_fit_error_usd` | MDG / fit error |
| `mdg_per_exposure_long_usd` | MDG / exposure long avg |
| `mdg_per_exposure_short_usd` | MDG / exposure short avg |
| `omega_ratio_usd` | Omega ratio (seuil = 0) |
| `peak_recovery_hours_equity_usd` | Temps de recovery moyen (heures) |
| `pnl_ratio_long_short` | Ratio PnL long / short |
| `position_held_hours_max` | Durée max de détention (heures) |
| `position_held_hours_mean` | Durée moyenne |
| `position_held_hours_median` | Durée médiane |
| `position_unchanged_hours_max` | Max heures sans changement de position |
| `positions_held_per_day` | Nb positions par jour |
| `sharpe_ratio_usd` | Sharpe annualisé |
| `sortino_ratio_usd` | Sortino (downside deviation) |
| `sterling_ratio_usd` | Sterling ratio |
| `volume_pct_per_day_avg` | Volume quotidien moyen / balance |

**Exclusions explicites** (ne pas implémenter) : `lpsm_*`, `roc_active_*`, `pnl_ratio_long_short`.

### 4.5 Output directory & files (backtest uniquement)

En mode `backtest` (CLI), le moteur crée un sous-dossier horodaté dans `output.dir` :

```
results/
└── 2026-06-26_16-42-32/
    ├── analysis.json              # Toutes les métriques du backtest
    ├── equity_chart.png           # Courbes equity (pnl cumulé) + balance (capital validé)
    ├── exposure_chart.png         # Capital engagé total au fil du temps
    ├── pnl_per_symbol.png         # PnL cumulé par symbole (une courbe par coin)
    └── data/
        ├── equity_curve.csv       # Données brutes (timestamp, equity, balance)
        ├── exposure.csv           # Données brutes (timestamp, exposure_usd, exposure_pct)
        └── pnl_symbol.csv         # Données brutes (timestamp, symbol, pnl_cumulé)
```

#### 4.5.1 Génération du dossier

- Le dossier est nommé au format `YYYY-MM-DD_HH-MM-SS` (locale UTC) au moment du lancement.
- Si le dossier existe déjà (cas improbable), suffixer avec `_N`.
- Tous les CSV sont écrits pendant la boucle de backtest (append à chaque itération ou batch à la fin).
- Les PNG sont générés **après** la fin de la boucle, en une seule passe, à partir des CSV.

#### 4.5.2 analysis.json

Contient un objet JSON avec toutes les métriques calculées (section 4.6), par exemple :

```json
{
  "backtest": {
    "date_from": "2024-01-01",
    "date_to": "2024-12-31",
    "timeframe": "1h",
    "symbols": ["BTCUSDT", "ETHUSDT"],
    "parameters": { ... }
  },
  "metrics": {
    "adg_usd": 12.34,
    "sharpe_ratio_usd": 1.56,
    ...
  }
}
```

#### 4.5.3 equity_chart.png

- Deux courbes sur le même graphique, en fonction du temps (axe X = dates du backtest).
  - **Equity** (vert / ligne pleine) : valeur totale du portefeuille au mark-to-market à chaque bougie.
  - **Balance** (bleu / ligne tiretée) : capital validé (réalisé, sans les PnL latents).
- Axe Y en USD.
- Titre : "Equity & Balance — BTCUSDT, ETHUSDT | 1h | 2024-01-01 → 2024-12-31".
- Légende, grille.

#### 4.5.4 exposure_chart.png

- Courbe du capital engagé total (somme des valeurs notionnelles de toutes les positions ouvertes) au fil du temps.
- Axe Y en USD.
- Ajouter une ligne horizontale à `initial_balance_usd` pour référence.
- Titre : "Capital Exposure".

#### 4.5.5 pnl_per_symbol.png

- Une courbe par symbole, représentant le PnL cumulé réalisé + latent pour chaque coin.
- Chaque symbole a une couleur distincte (palette dans `src/plot/colors.h`).
- Axe Y en USD.
- Légende.
- Titre : "Cumulative PnL per Symbol".

#### 4.5.6 Moteur de rendu

- Utilise **gnuplot** via `popen()` : le C++ écrit des commandes gnuplot sur un pipe.
- Gnuplot est appelé une fois par graphique (ou une seule session avec multi-plot).
- Sortie directe en PNG via le terminal `pngcairo`.
- **Dark theme** appliqué à tous les graphiques :
  - Fond : `#1a1a2e`
  - Bordure et grille : `#2d2d5e` / `#3a3a6e`
  - Texte (titres, axes, légende) : `#e0e0e0`
  - Courbe equity : `#00ff88`
  - Courbe balance : `#4a9eff` (tiretée)
  - Courbe exposition : `#ff6b35`
  - Palette symboles PnL : `['#00ff88', '#ff6b35', '#4a9eff', '#ffd93d', '#c084fc', '#ff4d6d', '#00d4aa', '#ffb347']`
  - Taille : 1600×900 px, police 14.
- **Dépendance** : `gnuplot` doit être installé sur le système. Le `CMakeLists.txt` vérifie sa présence avec `find_program` et émet un warning si absent (mais ne bloque pas la compilation). Un script `scripts/install_gnuplot.sh` est fourni.

### 4.6 Optimizer — Architecture mémoire partagée & parallélisation (`src/optimizer/`)

L'optimiseur est la partie la plus critique en performance. Chaque run de backtest partage les mêmes données de bougies (plusieurs centaines de Mo pour des périodes longues). L'architecture est conçue pour ne charger les données qu'**une seule fois** et les partager entre tous les workers.

#### 4.6.1 Principe : Shared-Nothing state, Shared-Everything data

```
 Bougies (mmap, read-only)
          │
          ▼
    ┌─────────────┐
    │  CandleStore │  <── chargé 1 seule fois en mémoire
    │  (const)     │
    └──────┬──────┘
           │ shared_ptr<const Candle[]>
           │
     ┌─────┼─────┐
     │     │     │
     ▼     ▼     ▼
   Worker Worker Worker   ThreadPool (n_workers)
     │     │     │
     │  Chacun a son state local (positions, equity, …)
     │  stack-allocé / thread_local, zero lock
     │
     ▼     ▼     ▼
   ┌──────────────────────┐
   │ ResultQueue (lock-free)│  ←─ metrics de chaque run
   └──────────────────────┘
```

#### 4.6.2 CandleStore + SymbolStore — données partagées

```cpp
// Chargé une fois au début de l'optimisation
class CandleStore {
public:
  std::span<const Candle> load(std::string_view cache_hash);  // mmap du .cache
  size_t trading_start_index() const;
  size_t total_count() const;
};

class SymbolStore {
public:
  const SymbolInfo& info(std::string_view symbol) const;
  // step_size, min_qty, min_notional, tick_size … chargés depuis SYMBOLS.bin
};
```

- Le `CandleStore` utilise `mmap` en lecture seule sur le fichier cache. Le kernel gère la pagination ; tous les threads du processus partagent les mêmes pages physiques.
- Le `SymbolStore` est un tableau constant de `SymbolInfo` partagé par tous les workers.
- Aucune copie des données entre workers : chaque worker reçoit un `std::span<const Candle>` + `const SymbolStore&`.

#### 4.6.3 ThreadPool avec work-stealing

- Pool de `n_workers` threads (défini `optimize.n_workers`, par défaut `std::thread::hardware_concurrency()`).
- File de tâches lock-free (mémoire locale + stealing) :
  - Une deque centrale (`ConcurrentQueue`) alimentée par le producteur (générateur de combinaisons).
  - Chaque worker possède une deque locale pour minimiser les contention.
  - Quand un worker vide sa deque locale, il vole des tâches chez un autre (random victim).
- Une tâche = une combinaison de paramètres + un index pour écrire le résultat.

#### 4.6.4 Pipeline détaillé

```
1. Charger les données   ──> CandleStore (1 appel mmap) + SymbolStore (1 appel)
2. Générer la grille     ──> N combinaisons de paramètres (std::array<double, M>)
3. Distribuer aux workers ──> ConcurrentQueue<ParamCombination>
4. Chaque worker :
   a. Pop une combinaison
   b. Copier les params dans son state local (stack)
   c. Exécuter Strategy::run(candle_span, symbol_store, params)
   d. Produire Metrics
   e. Filtrer par limits (si limit violée, ignorer)
   f. Calculer le score
   g. Push (index, score, metrics) dans la ResultQueue
5. Collecter les résultats, trier par score, garder top-N
```

#### 4.6.5 Stratégies avancées de parallélisation

| Technique | Application |
|---|---|
| **mmap partagé** | Les bougies sont mappées une fois, partagées entre tous les threads au niveau kernel (pages physiques identiques). |
| **Work-stealing** | Évite le déséquilibre de charge : un worker rapide vole des tâches à un worker lent. |
| **False sharing prevention** | Les résultats sont alignés sur 64 octets (cache line), chaque worker écrit dans sa propre partition d'un tableau pré-alloué. |
| **Branchless hot path** | La boucle de stratégie est écrite sans branches conditionnelles coûteuses (utilise `select`/masques logiques pour les remplacer). |
| **Prefetch** | `__builtin_prefetch` sur les bougies N+4 pendant le traitement de la bougie N. |
| **NUMA-aware** | Si `n_workers > 1` et système multi-socket : `pinning` des threads sur des cœurs distincts, allocation des pages dans le nœud local (via `libnuma`). |
| **Early pruning intégré** | Si `weighted_upnl <= sl_upnl_pct` déclenche un close total du symbole, la boucle ne visite plus les niveaux de close partielle pour ce symbole. Si plus aucune position ouverte, le symbole est ignoré jusqu'à la prochaine entrée. |

#### 4.6.6 Algorithme d'optimisation

- **Grid Search** : itération cartésienne sur toutes les combinaisons avec un pas fixe. Supporte les pas linéaires et logarithmiques (détecté automatiquement si les bornes sont `[0.01, 0.10]` → linéaire, `[10, 1000]` → user-chok).
- Extensible : l'interface `Optimizer` est conçue pour accueillir PSO ou Genetic Algorithm ultérieurement.

#### 4.6.7 Pas de sortie graphique

Pendant l'optimisation, aucun PNG n'est généré, aucun CSV d'equity n'est écrit. Seules les métriques sont calculées et stockées en mémoire. L'intégralité du temps CPU est consacrée au calcul des runs.

---

## 5. Performance & Optimisation C++

#### Compilateur & norme

- **Clang++** seul (version système, détecté par CMake). Pas de GCC.
- **C++23** (`-std=c++23`).
- RTTI désactivé (`-fno-rtti`) dans les hot paths.
- Exception handling désactivé (`-fno-exceptions`) si possible, sinon `-fno-unwind-tables`.

#### Flags de compilation (CMakeLists.txt)

```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE
  "-O3 -march=native -flto=thin -fstrict-aliasing -ffast-math -fno-rtti")

set(CMAKE_CXX_FLAGS_DEBUG
  "-O0 -g -fno-omit-frame-pointer")

# Flags stricts (erreur fatale sur tout ce qui est douteux)
set(WARNING_FLAGS
  -Wall -Wextra -Wpedantic
  -Werror                         # tout warning → erreur
  -Wunused-parameter              # paramètre non utilisé
  -Wunused-variable               # variable non utilisée
  -Wunused-function               # fonction non utilisée
  -Wunused-local-typedef          # typedef local non utilisé
  -Wconversion                    # conversion implicite
  -Wsign-conversion               # signe implicite
  -Wfloat-equal                   # comparaison float directe
  -Wshadow                        # variable cachée
  -Wnon-virtual-dtor              # destructeur non virtuel
  -Wold-style-cast                # cast C
  -Wcast-align                    # alignement de cast
  -Woverloaded-virtual            # override sans virtual
  -Wduplicated-cond               # condition dupliquée
  -Wlogical-op                    # &&/|| suspect
  -Wnull-dereference              # déréférencement nullptr
  -Wdouble-promotion              # float → double implicite
  -Wformat=2                      # format string sécurité
  -Weffc++                        # warning règles Effective C++
)
```

#### Règles de style & architecture

- **Aucun fichier .h/.cpp ne dépasse 500 lignes.** Si un module dépasse, il est refactoré en sous-modules (ex: `strategy/entry_grid.h`, `strategy/close_grid.h`, `strategy/stop_loss.h`).
- **Nomage métier** : les dossiers et fichiers portent le nom du concept (pas de `utils/misc.cpp`). Ex : `position_sizing.h`, `fee_handler.h`, `parkinson_volatility.h`.
- **Readabilité** : pas de macros (sauf include guards), pas de `auto` pour les types fondamentaux, pas de `template` méta-programmation dans le domaine métier.
- **Const-correctness** : tout paramètre non muté est `const`. Tout getter est `constexpr`/`noexcept`.
- **Pas d'héritage** (sauf interfaces minimales pour l'extensibilité de l'optimizer). Préférer le duck-typing via templates.
- **Toute fonction** a une documentation one-line (///) qui décrit le *quoi* pas le *comment*.

#### Optimisation bas niveau

- Profilage obligatoire : **perf** + **FlameGraphs**.
- **Aucune** virtual dispatch dans les hot paths (`final` classes, CRTP si polymorphisme nécessaire).
- `mmap` partagé pour les fichiers de cache (pages physiques uniques entre threads).
- Conteneurs : `std::vector` et `std::array` exclusivement dans les hot paths (pas de `map`/`set`).
- `small_vector` / `static_vector` pour les collections de petite taille (grilles, positions).
- Allocations : **pool allocator** pour les positions (chaque itération alloue/libère peu).
- String view (`std::string_view`) pour les symboles au runtime.
- `thread_local` pour les buffers temporaires et le state de la stratégie pendant l'optimisation.
- Computation des métriques vectorisée (SIMD si applicable, auto-vectorisation).
- L'EMA est calculée en `O(1)` par bougie (itératif).
- La volatilité de Parkinson est calculée en `O(1)` par bougie par symbole via fenêtre glissante.
- **Aucun mutex dans la boucle chaude** : la synchronisation est repoussée dans la queue de résultats lock-free.
- **Alignement cache line** (64 octets) sur toutes les structures partagées entre threads (falsesharing guard).
- **Thread pinning** explicite (`pthread_setaffinity_np`) si `n_workers` configuré.
- **Prefetch** logiciel (`__builtin_prefetch`) sur les itérations de bougies à l'avance.

---

## 6. Tests

- **Framework** : GoogleTest.
- **Tests unitaires** :
  - `Config Loader` : parsing correct, erreur sur champ invalide, calcul du warmup.
  - `CandleManager` : agrégation 1m → 1h, cache hit/miss, hash cohérent avec/sans warmup.
  - `Strategy` :
    - Double-down : sur des données synthétiques avec une baisse continue, vérifier que les entries sont placées à chaque `spacing`, chacune avec une taille = `slot_capital * initial_qty_pct * double_down_factor^k`.
    - Close grid : sur une hausse, vérifier que les `close_grid_count` closes partielles se déclenchent à `avg_price * (1 + k * spacing)` avec des quantités de `total_qty / close_grid_count`.
    - Step size : vérifier que l'arrondi `lot_step_size` est correct (ex: 0.001234 → 0.001).
    - Fees : vérifier que le PnL réalisé est réduit de `order_value * maker_fee_pct`.
    - Sélection par volatilité : avec 4 symboles et `n_positions=2`, seuls les 2 plus volatiles sont tradés.
    - EMA warmup : sur un jeu synthétique, vérifier que la première valeur de l'EMA au début du `trading_span` est correcte (identique à un calcul séquentiel sans warmup).
    - Vérifier qu'aucune position n'est ouverte pendant le warmup.
    - SL déclenché sur weighted_upnl.
  - `Metrics` : chaque métrique avec des données synthétiques, valeurs connues.
  - `Optimizer` : limites filtrées, score correct, parallélisme.
- **Test de bout en bout** :
  - Fichier JSON de test → backtest → métriques vérifiées (golden file).
  - Optimisation sur 2 paramètres → top result validé manuellement.
- **Benchmarks** : Google Benchmark, dossier `bench/`.

---

## 7. Instructions d'orchestration des sous-agents

Le développement est découpé en 6 phases séquentielles. Chaque phase est confiée à un sous-agent qui livre le code **testé et compilant**.

### Phase 1 — Squelette du projet & CMake

1. Créer l'arborescence complète (dossiers, `CMakeLists.txt` racine + tests).
2. Dépendances : `simdjson`, `libcurl`, `zstd`, `gtest`, `benchmark`.
3. `main.cpp` minimal : lit `argv[1]` (backtest|optimize) et `argv[2]` (chemin JSON), parse le JSON, dispatch.
4. **Vérification** : `cmake -B build && cmake --build build && ./build/martingale backtest test.json`.

### Phase 2 — Data Layer

1. Implémenter `BinanceClient` (download 1m, rate limit, découpage dates, `GET /exchangeInfo`).
2. Implémenter `symbol_info.h/.cpp` : parsing des filtres lot_size, min_notional, price_tick.
3. Implémenter `CandleManager` :
   - Téléchargement, warmup, agrégation, stockage `.bin.zst` (columnar + zstd).
   - Décompression à la volée via `ZSTD_decompress`.
   - `INDEX.bin` pour lookup rapide.
4. Implémenter `Cache` (hash xxHash3 avec warmup, format `.cache` non compressé, mmap).
5. **Vérification** : test unitaire qui télécharge 1 jour de BTCUSDT, aggrège en 1h, vérifie OHLC, vérifie le cache hit, vérifie que `SymbolInfo.step_size` est correct pour BTCUSDT (0.00001).

### Phase 3 — Strategy Engine

1. Implémenter `Strategy` :
   - Boucle principale vectorisée par symbole.
   - EMA itérative avec warmup.
   - Grille d'entries stateless : `entry_grid_spacing_pct` + `double_down_factor` + arrondi `step_size`.
   - Grille de closes stateless : `close_grid_spacing_pct` + `close_grid_count` + arrondi `step_size`.
   - Stop loss global `sl_upnl_pct`.
   - Filtre multi-coin par volatilité Parkinson (`n_positions`, `parkinson_volatility_span`).
   - Application des `maker_fee_pct` sur chaque ordre.
2. Implémenter `Metrics` (toutes les métriques listées, sauf les exclues).
3. **Vérification** :
   - Backtest 7 jours, 4 symboles, `n_positions=2` : seuls les 2 plus volatiles sont tradés.
   - Double-down : sur des données synthétiques, vérifier que la 2e entry est à `avg_price * (1 - spacing)` avec une taille × `double_down_factor`.
   - Step size : vérifier que `qty = 0.001234` avec `step_size = 0.001` → `0.001`.
   - Fees : vérifier que `fee = order_value * maker_fee_pct` est déduit du PnL.
   - Métriques vérifiées contre un calcul de référence (Python/R).

### Phase 4 — Optimizer

1. Implémenter `Optimizer` (Grid Search, limits, scoring, parallélisation).
2. **Vérification** : optimisation sur 2 paramètres avec 3 valeurs chacun → 9 runs, vérifier que le top score est cohérent, que les limits excluent les bons résultats.

### Phase 5 — Intégration & End-to-End

1. Câbler `main.cpp` : lire la config, dispatcher backtest ou optimize.
2. Ajouter la génération du hash de cache dans le pipeline.
3. Test end-to-end : fichier JSON → backtest → fichier metrics.json correct.
4. **Vérification** : comparer `metrics.json` avec golden file.

### Phase 6 — Profilage & Optimisation

1. Lancer `perf record` sur un backtest long (6 mois, 5 symboles).
2. Optimiser les 3 premiers hot spots identifiés.
3. Ré-écrire les points chauds avec SIMD manuel si besoin.
4. **Vérification** : benchmark avant/après, gain mesuré.

### Règles pour chaque agent

- Chaque agent doit **vérifier que le code compile** avant de livrer.
- Chaque agent doit **exécuter les tests** de sa phase et **confirmer qu'ils passent**.
- Le livrable de chaque phase est une **PR (Pull Request)** vers `main`.
- Aucune phase ne commence tant que la précédente n'a pas sa PR mergée et validée.

---

## 8. Définition of Done (DoD)

- [ ] Le projet compile avec `-Wall -Wextra -Wpedantic -Werror` sans warning.
- [ ] Tous les tests unitaires passent.
- [ ] Le test end-to-end produit un `metrics.json` identique au golden file.
- [ ] Un benchmark montre que le débit de traitement est > 1M candles/seconde.
- [ ] La documentation `README.md` explique comment configurer, builder et lancer.

---

## 9. Livrables

1. Code source complet dans `/src`.
2. Tests unitaires et end-to-end dans `/tests`.
3. `CMakeLists.txt` fonctionnel (Finder ou FetchContent pour les dépendances).
4. `config/schema.json` (JSON Schema).
5. `scripts/run_benchmark.sh` pour reproduire les mesures de perf.
6. `audit/test_no_lookahead.sh` + `audit/generate_lookahead_test_data.py` (cf. section 10).
7. `README.md` avec instructions de build et d'utilisation.

---

## 10. Audit de non-look-ahead bias

### Objectif

Garantir que le moteur de backtest n'utilise **aucune information future** (look-ahead bias) dans ses calculs. Le risque principal est qu'une bougie future influence l'EMA, la volatilité de Parkinson, ou les décisions d'entrée/sortie de la bougie courante.

### Architecture de l'audit

#### 10.1 Vérification statique (code review)

L'audit vérifie que :

1. **L'EMA** est calculée de façon purement itérative : `ema_i = α * close_i + (1-α) * ema_{i-1}`. Pas de ré-estimation sur tout le passé à chaque itération.
2. **La volatilité de Parkinson** utilise une fenêtre glissante stricte : seules les `N` bougies précédant la bougie courante sont utilisées.
3. **Les grilles de prise de bénéfice** utilisent `entry_price` de la position, pas le `close` de la bougie courante.
4. **Le stop loss** utilise l'UPnL mark-to-market basé sur le `close` actuel, pas le `close` futur.
5. **Les fichiers de cache** chargent les données de `data_start` à `date_to` sans accès à des données au-delà de la bougie courante pendant l'exécution.
6. **Le warmup** est correctement isolé : le `trading_span` commence exactement à `date_from`, et l'état des indicateurs à `t=trade_start` est identique à ce qu'il serait si on avait commencé à calculer à `t=data_start`.

#### 10.2 Tests automatisés d'audit

Des scripts dans `audit/` permettent de détecter automatiquement les look-ahead biases.

##### `audit/generate_lookahead_test_data.py`

Génère des données synthétiques avec des propriétés connues :

```python
# Génère N candles avec une tendance + bruit
# Pour chaque bougie i, un "signal" est défini comme f(i) + ε
# Permet de savoir EXACTEMENT quel signal était disponible à chaque t.
# Sortie : fichiers .bin.zst journaliers (même format que le CandleManager)
# dans data/candles/<SYM>/ et met à jour INDEX.bin.
```

##### `audit/test_no_lookahead.sh`

Script bash qui exécute une batterie de tests :

```bash
#!/usr/bin/env bash
# Test 1 : Vérification que l'EMA n'utilise pas les données futures
#   - Génère des données avec un saut brutal à t=100
#   - Vérifie qu'à t=99, l'EMA n'a PAS changé (pas de fuite future)
#
# Test 2 : Vérification de la fenêtre de volatilité
#   - Génère des données avec un pic de volatilité artificiel à t=50
#   - Vérifie qu'à t=50+parkinson_span, la volatilité augmente
#   - Vérifie qu'à t < 50, la volatilité est basse
#
# Test 3 : Vérification que les trades ne commencent qu'à date_from
#   - Lance un backtest avec date_from = t=100
#   - Vérifie que le nombre de trades avant t=100 est 0
#
# Test 4 : Test de décalage temporel (time-shift test)
#   - Lance un backtest normal sur [date_from, date_to]
#   - Relance le même backtest avec les données décalées de +1 bougie
#   - Vérifie que les positions ouvertes à t dans le premier run
#     correspondent aux positions ouvertes à t+1 dans le second run
#     (cohérence temporelle)
#
# Test 5 : Purge du cache et re-vérification
#   - Supprime le cache, relance le backtest
#   - Vérifie que les métriques sont identiques (pas de dépendance au cache)
```

#### 10.3 Time-shift test (détection automatique de fuite)

Principe :

1. Exécuter un backtest sur la période `[T0, T1]` → obtient une séquence de trades `{(t_i, side, size, price)}`.
2. Créer un jeu de données identique mais décalé de +1 bougie dans le temps (insérer une bougie factice au début, retirer la dernière).
3. Exécuter le même backtest sur `[T0+1, T1+1]` (même nombre de bougies).
4. **Si le backtest est sans biais**, les trades devraient être identiques aux mêmes index (juste décalés de 1 en timestamp).
5. **Si un look-ahead bias existe** (par exemple une EMA qui utilise la bougie courante comme si elle était la future), alors les trades différeront entre les deux runs.

Ce test est automatisé dans `audit/test_no_lookahead.sh` et doit passer avant toute mise en production.

#### 10.4 Procédure d'audit manuelle (avant release)

1. `git checkout main && cmake -B build && cmake --build build`
2. `cd audit && python3 generate_lookahead_test_data.py --output ../data/candles/`
3. `bash test_no_lookahead.sh`
4. Vérifier que tous les tests retournent `PASS`.
5. En cas d'échec, identifier le module incriminé via les logs de time-shift.
6. Corriger et re-exécuter jusqu'à `ALL PASS`.

### Livrables de l'audit

- `audit/generate_lookahead_test_data.py` : générateur de données synthétiques.
- `audit/test_no_lookahead.sh` : script de tests automatisés.
- Résultat de l'audit annexé à chaque release (fichier `audit/report_<version>.txt`).

---

## 11. Glossaire

| Terme | Définition |
|---|---|
| Warmup | Période avant `date_from` utilisée uniquement pour initialiser les indicateurs (EMA, volatilité). Aucune position n'est ouverte. |
| Parkinson Volatility | Estimateur de volatilité utilisant les prix High/Low, plus robuste que le close-to-close. |
| Look-ahead bias | Utilisation incorrecte d'une information qui n'aurait pas été disponible au moment de la décision. |
| Grid (entry/close) | Ensemble de niveaux de prix prédéfinis exprimés en pourcentage. |
| UPnL | Unrealized Profit and Loss (profit/perte latente). |
