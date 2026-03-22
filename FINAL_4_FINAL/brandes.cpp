/**
 * ============================================================================
 *  Brandes' Algorithm for Betweenness Centrality
 * ============================================================================
 *
 *  Reference:
 *    Ulrik Brandes, "A faster algorithm for betweenness centrality",
 *    Journal of Mathematical Sociology 25(2):163-177, 2001.
 *
 *  Supports:
 *    - Unweighted graphs  : O(n * m) time,   O(n + m) space
 *    - Weighted graphs     : O(n*m + n^2 log n) time, O(n + m) space
 *    - Directed / Undirected
 *
 *  Input formats supported:
 *    - Edge list:  each line "u v [w]"  (0-indexed or 1-indexed, auto-detected)
 *    - Optionally first line: "n m" (number of nodes, edges)
 *
 *  Usage:
 *    brandes [options] <input_file>
 *
 *  Options:
 *    -d          : directed graph
 *    -u          : undirected graph (default)
 *    -w          : weighted graph (default: unweighted)
 *    -1          : vertices are 1-indexed in input (default: 0-indexed)
 *    -o <file>   : output file (default: stdout)
 *    -top <k>    : print only top-k vertices by centrality
 *    -normalize  : normalize scores to [0, 1]
 *
 *  Compile:
 *    g++ -O2 -std=c++17 -o brandes brandes.cpp
 *
 * ============================================================================
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <queue>
#include <stack>
#include <string>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <tuple>

// Platform-specific memory measurement
#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
  #pragma comment(lib, "psapi.lib")
#elif defined(__linux__)
  #include <fstream>
  // /proc/self/status for VmRSS
#endif

// ---------------------------------------------------------------------------
//  Memory measurement utilities
// ---------------------------------------------------------------------------
struct MemoryInfo {
    size_t peak_rss_bytes;    // Peak resident set size (physical memory)
    size_t current_rss_bytes; // Current RSS
    size_t estimated_graph_bytes; // Estimated memory for graph data structures
};

size_t get_peak_rss() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.substr(0, 6) == "VmHWM:") {
            size_t kb = 0;
            std::sscanf(line.c_str(), "VmHWM: %zu kB", &kb);
            return kb * 1024;
        }
    }
    return 0;
#else
    return 0;
#endif
}

size_t get_current_rss() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            size_t kb = 0;
            std::sscanf(line.c_str(), "VmRSS: %zu kB", &kb);
            return kb * 1024;
        }
    }
    return 0;
#else
    return 0;
#endif
}

// Estimate memory used by the graph + algorithm data structures
size_t estimate_algorithm_memory(int n, int m_directed) {
    // Graph adjacency list: n vectors + m_directed Edge structs
    // Edge struct = int(4) + double(8) + padding = 16 bytes typically
    size_t graph_mem = n * sizeof(std::vector<int>) + m_directed * 16;
    // Per-source BFS/Dijkstra arrays (re-used, so count once):
    //   stack<int>: O(n), vector<vector<int>> P: O(n), sigma: O(n),
    //   dist: O(n), delta: O(n), queue: O(n)
    size_t per_source = 6 * n * sizeof(double); // rough estimate
    // CB array: n doubles
    size_t cb_mem = n * sizeof(double);
    return graph_mem + per_source + cb_mem;
}

std::string format_bytes(size_t bytes) {
    if (bytes >= 1024ULL * 1024 * 1024)
        return std::to_string(bytes / (1024ULL * 1024 * 1024)) + "." +
               std::to_string((bytes % (1024ULL * 1024 * 1024)) * 10 / (1024ULL * 1024 * 1024)) + " GB";
    if (bytes >= 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024)) + "." +
               std::to_string((bytes % (1024 * 1024)) * 10 / (1024 * 1024)) + " MB";
    if (bytes >= 1024)
        return std::to_string(bytes / 1024) + "." +
               std::to_string((bytes % 1024) * 10 / 1024) + " KB";
    return std::to_string(bytes) + " B";
}

// ---------------------------------------------------------------------------
//  Graph representation
// ---------------------------------------------------------------------------
struct Edge {
    int to;
    double weight;
};

struct Graph {
    int n;                              // number of vertices
    int m;                              // number of edges (directed count)
    bool directed;
    bool weighted;
    std::vector<std::vector<Edge>> adj; // adjacency list
    std::vector<int> idx_to_id;         // internal index -> original node ID

    Graph() : n(0), m(0), directed(false), weighted(false) {}

    void init(int num_vertices, bool is_directed, bool is_weighted) {
        n = num_vertices;
        m = 0;
        directed = is_directed;
        weighted = is_weighted;
        adj.assign(n, {});
        idx_to_id.clear();
    }

    void add_edge(int u, int v, double w = 1.0) {
        adj[u].push_back({v, w});
        m++;
        if (!directed) {
            adj[v].push_back({u, w});
            m++;
        }
    }
};

// ---------------------------------------------------------------------------
//  Brandes' Algorithm — Unweighted BFS variant
// ---------------------------------------------------------------------------
void brandes_bfs_single_source(const Graph& G, int s, std::vector<double>& CB) {
    const int n = G.n;

    // Data structures (per SSSP call)
    std::stack<int> S;                       // vertices in order of non-increasing distance
    std::vector<std::vector<int>> P(n);      // predecessors on shortest paths
    std::vector<long long> sigma(n, 0);      // number of shortest paths from s
    std::vector<int> dist(n, -1);            // distance from s
    std::vector<double> delta(n, 0.0);       // dependency of s on v

    // BFS initialization
    sigma[s] = 1;
    dist[s] = 0;
    std::queue<int> Q;
    Q.push(s);

    // BFS traversal
    while (!Q.empty()) {
        int v = Q.front(); Q.pop();
        S.push(v);
        for (const auto& e : G.adj[v]) {
            int w = e.to;
            // w found for first time?
            if (dist[w] < 0) {
                Q.push(w);
                dist[w] = dist[v] + 1;
            }
            // shortest path to w via v?
            if (dist[w] == dist[v] + 1) {
                sigma[w] += sigma[v];
                P[w].push_back(v);
            }
        }
    }

    // Back-propagation of dependencies (Theorem 6)
    // S returns vertices in order of non-increasing distance from s
    while (!S.empty()) {
        int w = S.top(); S.pop();
        for (int v : P[w]) {
            delta[v] += (static_cast<double>(sigma[v]) / sigma[w]) * (1.0 + delta[w]);
        }
        if (w != s) {
            CB[w] += delta[w];
        }
    }
}

// ---------------------------------------------------------------------------
//  Brandes' Algorithm — Weighted Dijkstra variant
// ---------------------------------------------------------------------------
void brandes_dijkstra_single_source(const Graph& G, int s, std::vector<double>& CB) {
    const int n = G.n;

    std::stack<int> S;
    std::vector<std::vector<int>> P(n);
    std::vector<long long> sigma(n, 0);
    std::vector<double> dist(n, -1.0);       // -1 means infinity
    std::vector<double> delta(n, 0.0);

    sigma[s] = 1;
    dist[s] = 0.0;

    // Min-heap: (distance, vertex)
    using pdi = std::pair<double, int>;
    std::priority_queue<pdi, std::vector<pdi>, std::greater<pdi>> PQ;
    PQ.push(pdi(0.0, s));

    while (!PQ.empty()) {
        pdi top = PQ.top(); PQ.pop();
        double d_v = top.first;
        int v = top.second;

        // Skip if we already found a shorter path
        if (d_v > dist[v] && dist[v] >= 0) continue;

        S.push(v);

        for (const auto& e : G.adj[v]) {
            int w = e.to;
            double new_dist = dist[v] + e.weight;

            // Path discovery: w found for first time, or shorter path found
            if (dist[w] < 0 || new_dist < dist[w]) {
                dist[w] = new_dist;
                sigma[w] = sigma[v];
                P[w].clear();
                P[w].push_back(v);
                PQ.push(pdi(new_dist, w));
            }
            // Another shortest path to w via v
            else if (std::abs(new_dist - dist[w]) < 1e-12) {
                sigma[w] += sigma[v];
                P[w].push_back(v);
            }
        }
    }

    // Back-propagation of dependencies (Theorem 6)
    while (!S.empty()) {
        int w = S.top(); S.pop();
        for (int v : P[w]) {
            delta[v] += (static_cast<double>(sigma[v]) / sigma[w]) * (1.0 + delta[w]);
        }
        if (w != s) {
            CB[w] += delta[w];
        }
    }
}

// ---------------------------------------------------------------------------
//  Compute betweenness centrality for all vertices
// ---------------------------------------------------------------------------
std::vector<double> brandes_betweenness(const Graph& G, bool verbose = true) {
    std::vector<double> CB(G.n, 0.0);

    auto t_start = std::chrono::high_resolution_clock::now();
    auto last_report = t_start;
    const double report_interval_sec = 30.0;

    for (int s = 0; s < G.n; s++) {
        if (G.weighted) {
            brandes_dijkstra_single_source(G, s, CB);
        } else {
            brandes_bfs_single_source(G, s, CB);
        }

        if (verbose) {
            auto t_now = std::chrono::high_resolution_clock::now();
            double since_last = std::chrono::duration<double>(t_now - last_report).count();
            bool is_last = (s + 1 == G.n);

            if (is_last || since_last >= report_interval_sec) {
                double elapsed = std::chrono::duration<double>(t_now - t_start).count();
                double pct = 100.0 * (s + 1) / G.n;
                double eta = elapsed / (s + 1) * (G.n - s - 1);
                std::cerr << "\r  Progress: " << std::fixed << std::setprecision(2)
                          << pct << "%  |  Elapsed: " << elapsed
                          << "s  |  ETA: " << eta
                          << "s  |  Sources: " << (s + 1) << "/" << G.n << "   "
                          << std::flush;
                last_report = t_now;
            }
        }
    }

    // For undirected graphs, each shortest path s->t is counted twice (once
    // from s, once from t), so divide all scores by 2.
    if (!G.directed) {
        for (auto& v : CB) v /= 2.0;
    }

    if (verbose) {
        auto t_end = std::chrono::high_resolution_clock::now();
        double total = std::chrono::duration<double>(t_end - t_start).count();
        std::cerr << "\r  Completed in " << std::fixed << std::setprecision(3)
                  << total << " seconds.                         " << std::endl;
    }

    return CB;
}

// ---------------------------------------------------------------------------
//  Naive O(n^3) baseline for verification (only for small graphs)
// ---------------------------------------------------------------------------
std::vector<double> naive_betweenness(const Graph& G) {
    const int n = G.n;
    // Floyd-Warshall for all-pairs shortest paths
    const double INF = 1e18;
    std::vector<std::vector<double>> dist(n, std::vector<double>(n, INF));
    std::vector<std::vector<long long>> cnt(n, std::vector<long long>(n, 0));

    for (int i = 0; i < n; i++) {
        dist[i][i] = 0;
        cnt[i][i] = 1;
    }
    for (int u = 0; u < n; u++) {
        for (const auto& e : G.adj[u]) {
            if (G.directed || u < e.to || dist[u][e.to] == e.weight) {
                // Handle potential multiple edges
            }
            if (e.weight < dist[u][e.to]) {
                dist[u][e.to] = e.weight;
                cnt[u][e.to] = 1;
                if (!G.directed) {
                    dist[e.to][u] = e.weight;
                    cnt[e.to][u] = 1;
                }
            } else if (std::abs(e.weight - dist[u][e.to]) < 1e-12) {
                cnt[u][e.to]++;
                if (!G.directed) cnt[e.to][u]++;
            }
        }
    }

    for (int k = 0; k < n; k++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                double through_k = dist[i][k] + dist[k][j];
                if (through_k < dist[i][j]) {
                    dist[i][j] = through_k;
                    cnt[i][j] = cnt[i][k] * cnt[k][j];
                } else if (std::abs(through_k - dist[i][j]) < 1e-12 && k != i && k != j) {
                    // Floyd-Warshall path counting is tricky; skip for simplicity
                }
            }
        }
    }

    // Compute betweenness by definition
    // For each v, CB(v) = sum_{s!=v!=t} sigma_st(v)/sigma_st
    // Use BFS/Dijkstra from each source for correct counting
    std::vector<double> CB(n, 0.0);

    for (int s = 0; s < n; s++) {
        // Single-source shortest paths via BFS/Dijkstra
        std::vector<double> d(n, INF);
        std::vector<long long> sigma(n, 0);
        d[s] = 0; sigma[s] = 1;

        if (!G.weighted) {
            std::queue<int> Q;
            Q.push(s);
            while (!Q.empty()) {
                int u = Q.front(); Q.pop();
                for (const auto& e : G.adj[u]) {
                    if (d[e.to] == INF) {
                        d[e.to] = d[u] + 1;
                        Q.push(e.to);
                    }
                    if (std::abs(d[e.to] - d[u] - 1) < 1e-12) {
                        sigma[e.to] += sigma[u];
                    }
                }
            }
        } else {
            using pdi = std::pair<double, int>;
            std::priority_queue<pdi, std::vector<pdi>, std::greater<pdi>> PQ;
            PQ.push(pdi(0.0, s));
            while (!PQ.empty()) {
                pdi top = PQ.top(); PQ.pop();
                double du = top.first; int u = top.second;
                if (du > d[u]) continue;
                for (const auto& e : G.adj[u]) {
                    double nd = d[u] + e.weight;
                    if (nd < d[e.to]) {
                        d[e.to] = nd;
                        sigma[e.to] = sigma[u];
                        PQ.push(pdi(nd, e.to));
                    } else if (std::abs(nd - d[e.to]) < 1e-12) {
                        sigma[e.to] += sigma[u];
                    }
                }
            }
        }

        // For each pair (s,t), check each v
        for (int t = 0; t < n; t++) {
            if (t == s) continue;
            for (int v = 0; v < n; v++) {
                if (v == s || v == t) continue;
                if (d[v] < INF && d[t] < INF &&
                    std::abs(d[s] + d[v] - d[v]) < 1e-12) {  // d[s]=0 trivially
                    // Check Bellman criterion: d(s,t) == d(s,v) + d(v,t)
                    // We need d(v,t), compute it separately — too expensive
                }
            }
        }
    }

    // Actually, let's do it properly with the pair-dependency approach:
    // Recompute using all-pairs BFS
    // dist[s][t] and sigma[s][t] from SSSP
    std::vector<std::vector<double>> dd(n);
    std::vector<std::vector<long long>> ss(n);

    for (int s = 0; s < n; s++) {
        dd[s].assign(n, INF);
        ss[s].assign(n, 0);
        dd[s][s] = 0; ss[s][s] = 1;

        if (!G.weighted) {
            std::queue<int> Q;
            Q.push(s);
            while (!Q.empty()) {
                int u = Q.front(); Q.pop();
                for (const auto& e : G.adj[u]) {
                    if (dd[s][e.to] == INF) {
                        dd[s][e.to] = dd[s][u] + 1;
                        Q.push(e.to);
                    }
                    if (std::abs(dd[s][e.to] - dd[s][u] - 1) < 1e-12) {
                        ss[s][e.to] += ss[s][u];
                    }
                }
            }
        } else {
            using pdi = std::pair<double, int>;
            std::priority_queue<pdi, std::vector<pdi>, std::greater<pdi>> PQ;
            PQ.push(pdi(0.0, s));
            while (!PQ.empty()) {
                pdi top = PQ.top(); PQ.pop();
                double du = top.first; int u = top.second;
                if (du > dd[s][u]) continue;
                for (const auto& e : G.adj[u]) {
                    double nd = dd[s][u] + e.weight;
                    if (nd < dd[s][e.to]) {
                        dd[s][e.to] = nd;
                        ss[s][e.to] = ss[s][u];
                        PQ.push(pdi(nd, e.to));
                    } else if (std::abs(nd - dd[s][e.to]) < 1e-12) {
                        ss[s][e.to] += ss[s][u];
                    }
                }
            }
        }
    }

    // Now compute CB by summing pair-dependencies
    CB.assign(n, 0.0);
    for (int s = 0; s < n; s++) {
        for (int t = 0; t < n; t++) {
            if (s == t) continue;
            if (ss[s][t] == 0) continue;
            for (int v = 0; v < n; v++) {
                if (v == s || v == t) continue;
                // Bellman criterion: v lies on shortest s-t path iff d(s,t) = d(s,v) + d(v,t)
                if (dd[s][v] < INF && dd[v][t] < INF &&
                    std::abs(dd[s][t] - dd[s][v] - dd[v][t]) < 1e-12) {
                    // sigma_st(v) = sigma_sv * sigma_vt
                    double pair_dep = (double)(ss[s][v] * ss[v][t]) / ss[s][t];
                    CB[v] += pair_dep;
                }
            }
        }
    }

    // For undirected graphs, each pair (s,t) counted twice
    if (!G.directed) {
        for (auto& v : CB) v /= 2.0;
    }

    return CB;
}

// ---------------------------------------------------------------------------
//  Random graph generators for testing
// ---------------------------------------------------------------------------
Graph generate_erdos_renyi(int n, double density, bool directed = false, bool weighted = false) {
    Graph G;
    G.init(n, directed, weighted);
    srand(42);
    for (int u = 0; u < n; u++) {
        int start = directed ? 0 : u + 1;
        for (int v = start; v < n; v++) {
            if (u == v) continue;
            if ((double)rand() / RAND_MAX < density) {
                double w = weighted ? (1.0 + (double)(rand() % 100)) : 1.0;
                G.add_edge(u, v, w);
            }
        }
    }
    return G;
}

Graph generate_sparse_graph(int n, int avg_degree, bool directed = false, bool weighted = false) {
    Graph G;
    G.init(n, directed, weighted);
    srand(42);

    // Generate a spanning tree first (to ensure connectivity)
    for (int i = 1; i < n; i++) {
        int j = rand() % i;
        double w = weighted ? (1.0 + (double)(rand() % 100)) : 1.0;
        G.add_edge(j, i, w);
    }

    // Add random edges to reach desired average degree
    int target_edges = (avg_degree * n) / (directed ? 1 : 2);
    int current_edges = n - 1;
    std::unordered_set<long long> existing;
    for (int u = 0; u < n; u++) {
        for (const auto& e : G.adj[u]) {
            long long key = (long long)std::min(u, e.to) * n + std::max(u, e.to);
            existing.insert(key);
        }
    }

    while (current_edges < target_edges) {
        int u = rand() % n;
        int v = rand() % n;
        if (u == v) continue;
        long long key = (long long)std::min(u, v) * n + std::max(u, v);
        if (existing.count(key)) continue;
        existing.insert(key);
        double w = weighted ? (1.0 + (double)(rand() % 100)) : 1.0;
        G.add_edge(u, v, w);
        current_edges++;
    }
    return G;
}

// ---------------------------------------------------------------------------
//  I/O: Read graph from file
// ---------------------------------------------------------------------------
Graph read_graph(const std::string& filename, bool directed, bool weighted, bool one_indexed) {
    std::ifstream fin(filename);
    if (!fin.is_open()) {
        std::cerr << "Error: Cannot open file '" << filename << "'" << std::endl;
        exit(1);
    }

    std::vector<std::tuple<int, int, double>> edges;
    std::string line;

    while (std::getline(fin, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;

        std::istringstream iss(line);

        int u, v;
        double w = 1.0;
        iss >> u >> v;
        if (iss.fail()) continue;
        if (weighted) {
            iss >> w;
            if (iss.fail()) w = 1.0;
        }

        if (one_indexed) { u--; v--; }
        if (u < 0 || v < 0) continue;

        edges.push_back({u, v, w});
    }
    fin.close();

    if (!directed) {
        for (auto& edge : edges) {
            int u = std::get<0>(edge);
            int v = std::get<1>(edge);
            if (u > v) {
                std::swap(u, v);
                std::get<0>(edge) = u;
                std::get<1>(edge) = v;
            }
        }

        std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
            if (std::get<1>(a) != std::get<1>(b)) return std::get<1>(a) < std::get<1>(b);
            return std::get<2>(a) < std::get<2>(b);
        });

        edges.erase(std::unique(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
            return std::get<0>(a) == std::get<0>(b)
                && std::get<1>(a) == std::get<1>(b)
                && std::abs(std::get<2>(a) - std::get<2>(b)) < 1e-12;
        }), edges.end());
    }

    std::unordered_map<int, int> id_to_idx;
    std::vector<int> idx_to_id;
    id_to_idx.reserve(edges.size() * 2 + 1);
    idx_to_id.reserve(edges.size() + 1);

    auto get_or_add = [&](int original_id) {
        auto it = id_to_idx.find(original_id);
        if (it != id_to_idx.end()) return it->second;
        int idx = static_cast<int>(idx_to_id.size());
        id_to_idx[original_id] = idx;
        idx_to_id.push_back(original_id);
        return idx;
    };

    std::vector<std::tuple<int, int, double>> remapped_edges;
    remapped_edges.reserve(edges.size());
    for (const auto& edge : edges) {
        int u = get_or_add(std::get<0>(edge));
        int v = get_or_add(std::get<1>(edge));
        remapped_edges.push_back({u, v, std::get<2>(edge)});
    }

    Graph G;
    G.init(static_cast<int>(idx_to_id.size()), directed, weighted);
    G.idx_to_id = std::move(idx_to_id);
    for (const auto& edge : remapped_edges) {
        G.add_edge(std::get<0>(edge), std::get<1>(edge), std::get<2>(edge));
    }

    return G;
}

// ---------------------------------------------------------------------------
//  Output results
// ---------------------------------------------------------------------------
void print_results(const Graph& G, const std::vector<double>& CB, bool normalize,
                   int top_k, std::ostream& out) {
    const int n = G.n;
    double norm_factor = 1.0;
    if (normalize && n > 2) {
        // Maximum possible betweenness for undirected: (n-1)(n-2)/2
        // For directed: (n-1)(n-2)
        norm_factor = G.directed
            ? (double)(n - 1) * (n - 2)
            : (double)(n - 1) * (n - 2) / 2.0;
    }

    // Create index array for sorting
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return CB[a] > CB[b];
    });

    int count = (top_k > 0) ? std::min(top_k, n) : n;

    out << std::fixed << std::setprecision(6);
    out << "Rank\tNode ID\tBetweenness";
    if (normalize) out << " (normalized)";
    out << "\n";
    out << "----\t-------\t-----------\n";

    for (int i = 0; i < count; i++) {
        int v = idx[i];
        int node_id = (v < static_cast<int>(G.idx_to_id.size())) ? G.idx_to_id[v] : v;
        double score = normalize ? CB[v] / norm_factor : CB[v];
        out << (i + 1) << "\t" << node_id << "\t" << score << "\n";
    }
}

// ---------------------------------------------------------------------------
//  Verification (small graphs)
// ---------------------------------------------------------------------------
bool verify_small(bool verbose = true) {
    if (verbose) std::cerr << "Running verification on small test graphs...\n";

    // Test 1: Simple path graph  0 - 1 - 2 - 3 - 4
    {
        Graph G;
        G.init(5, false, false);
        G.add_edge(0, 1);
        G.add_edge(1, 2);
        G.add_edge(2, 3);
        G.add_edge(3, 4);

        auto CB = brandes_betweenness(G, false);
        auto CB_naive = naive_betweenness(G);

        for (int i = 0; i < 5; i++) {
            if (std::abs(CB[i] - CB_naive[i]) > 1e-6) {
                std::cerr << "  FAIL: Path graph, vertex " << i
                          << " brandes=" << CB[i] << " naive=" << CB_naive[i] << "\n";
                return false;
            }
        }
        if (verbose) std::cerr << "  PASS: Path graph (5 vertices)\n";
    }

    // Test 2: Star graph  0 is center, connected to 1,2,3,4
    {
        Graph G;
        G.init(5, false, false);
        G.add_edge(0, 1);
        G.add_edge(0, 2);
        G.add_edge(0, 3);
        G.add_edge(0, 4);

        auto CB = brandes_betweenness(G, false);
        auto CB_naive = naive_betweenness(G);

        for (int i = 0; i < 5; i++) {
            if (std::abs(CB[i] - CB_naive[i]) > 1e-6) {
                std::cerr << "  FAIL: Star graph, vertex " << i
                          << " brandes=" << CB[i] << " naive=" << CB_naive[i] << "\n";
                return false;
            }
        }
        if (verbose) std::cerr << "  PASS: Star graph (5 vertices)\n";
    }

    // Test 3: Complete graph K5
    {
        Graph G;
        G.init(5, false, false);
        for (int i = 0; i < 5; i++)
            for (int j = i + 1; j < 5; j++)
                G.add_edge(i, j);

        auto CB = brandes_betweenness(G, false);
        auto CB_naive = naive_betweenness(G);

        for (int i = 0; i < 5; i++) {
            if (std::abs(CB[i] - CB_naive[i]) > 1e-6) {
                std::cerr << "  FAIL: K5 graph, vertex " << i
                          << " brandes=" << CB[i] << " naive=" << CB_naive[i] << "\n";
                return false;
            }
        }
        if (verbose) std::cerr << "  PASS: Complete graph K5\n";
    }

    // Test 4: Random graph with 50 vertices, 10% density
    {
        Graph G = generate_erdos_renyi(50, 0.1);
        auto CB = brandes_betweenness(G, false);
        auto CB_naive = naive_betweenness(G);

        for (int i = 0; i < 50; i++) {
            if (std::abs(CB[i] - CB_naive[i]) > 1e-4) {
                std::cerr << "  FAIL: Random graph(50,0.1), vertex " << i
                          << " brandes=" << CB[i] << " naive=" << CB_naive[i] << "\n";
                return false;
            }
        }
        if (verbose) std::cerr << "  PASS: Random graph (50 vertices, 10% density)\n";
    }

    // Test 5: Weighted random graph
    {
        Graph G = generate_erdos_renyi(30, 0.15, false, true);
        auto CB = brandes_betweenness(G, false);
        auto CB_naive = naive_betweenness(G);

        for (int i = 0; i < 30; i++) {
            if (std::abs(CB[i] - CB_naive[i]) > 1e-4) {
                std::cerr << "  FAIL: Weighted random graph(30,0.15), vertex " << i
                          << " brandes=" << CB[i] << " naive=" << CB_naive[i] << "\n";
                return false;
            }
        }
        if (verbose) std::cerr << "  PASS: Weighted random graph (30 vertices, 15% density)\n";
    }

    // Test 6: Directed random graph
    {
        Graph G = generate_erdos_renyi(40, 0.1, true, false);
        auto CB = brandes_betweenness(G, false);
        auto CB_naive = naive_betweenness(G);

        for (int i = 0; i < 40; i++) {
            if (std::abs(CB[i] - CB_naive[i]) > 1e-4) {
                std::cerr << "  FAIL: Directed random graph(40,0.1), vertex " << i
                          << " brandes=" << CB[i] << " naive=" << CB_naive[i] << "\n";
                return false;
            }
        }
        if (verbose) std::cerr << "  PASS: Directed random graph (40 vertices, 10% density)\n";
    }

    if (verbose) std::cerr << "All verification tests PASSED.\n\n";
    return true;
}

// ---------------------------------------------------------------------------
//  Performance benchmark
// ---------------------------------------------------------------------------
void run_benchmark() {
    std::cerr << "=============================================================\n";
    std::cerr << "  Performance Benchmark: Brandes' Algorithm\n";
    std::cerr << "=============================================================\n\n";

    // Benchmark 1: Varying size with constant density
    std::cerr << "--- Benchmark 1: Varying size, density = 5% (unweighted, undirected) ---\n";
    std::cerr << std::setw(8) << "n" << std::setw(10) << "m"
              << std::setw(14) << "Time (s)" << std::setw(16) << "Throughput"
              << "\n";
    std::cerr << std::string(50, '-') << "\n";

    for (int n : {500, 1000, 2000, 3000, 5000}) {
        Graph G = generate_erdos_renyi(n, 0.05);

        auto t0 = std::chrono::high_resolution_clock::now();
        auto CB = brandes_betweenness(G, false);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        int edge_count = G.m / 2; // undirected
        std::cerr << std::setw(8) << n << std::setw(10) << edge_count
                  << std::setw(14) << std::fixed << std::setprecision(3) << elapsed
                  << std::setw(16) << std::setprecision(0) << (double)n * edge_count / elapsed
                  << "\n";
    }

    // Benchmark 2: Sparse graphs (constant average degree)
    std::cerr << "\n--- Benchmark 2: Sparse graphs, avg degree = 20 (unweighted, undirected) ---\n";
    std::cerr << std::setw(8) << "n" << std::setw(10) << "m"
              << std::setw(14) << "Time (s)" << std::setw(16) << "Throughput"
              << "\n";
    std::cerr << std::string(50, '-') << "\n";

    for (int n : {1000, 2000, 5000, 10000, 20000}) {
        Graph G = generate_sparse_graph(n, 20);

        auto t0 = std::chrono::high_resolution_clock::now();
        auto CB = brandes_betweenness(G, false);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        int edge_count = G.m / 2;
        std::cerr << std::setw(8) << n << std::setw(10) << edge_count
                  << std::setw(14) << std::fixed << std::setprecision(3) << elapsed
                  << std::setw(16) << std::setprecision(0) << (double)n * edge_count / elapsed
                  << "\n";
    }

    // Benchmark 3: Weighted vs Unweighted
    std::cerr << "\n--- Benchmark 3: Weighted vs Unweighted (n=2000, density=5%) ---\n";
    {
        Graph G_uw = generate_erdos_renyi(2000, 0.05, false, false);
        auto t0 = std::chrono::high_resolution_clock::now();
        brandes_betweenness(G_uw, false);
        auto t1 = std::chrono::high_resolution_clock::now();
        double t_uw = std::chrono::duration<double>(t1 - t0).count();

        Graph G_w = generate_erdos_renyi(2000, 0.05, false, true);
        t0 = std::chrono::high_resolution_clock::now();
        brandes_betweenness(G_w, false);
        t1 = std::chrono::high_resolution_clock::now();
        double t_w = std::chrono::duration<double>(t1 - t0).count();

        std::cerr << "  Unweighted (BFS):      " << std::fixed << std::setprecision(3) << t_uw << " s\n";
        std::cerr << "  Weighted   (Dijkstra): " << std::fixed << std::setprecision(3) << t_w  << " s\n";
        std::cerr << "  Ratio:                 " << std::setprecision(2) << t_w / t_uw << "x\n";
    }

    std::cerr << "\n=============================================================\n";
    std::cerr << "  Benchmark complete.\n";
    std::cerr << "=============================================================\n";
}

// ---------------------------------------------------------------------------
//  Usage
// ---------------------------------------------------------------------------
void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options] <input_file>\n"
              << "\nOptions:\n"
              << "  -d           Directed graph\n"
              << "  -u           Undirected graph (default)\n"
              << "  -w           Weighted graph (default: unweighted)\n"
              << "  -1           Vertices are 1-indexed (default: 0-indexed)\n"
              << "  -o <file>    Output file (default: stdout)\n"
              << "  -top <k>     Print only top-k vertices\n"
              << "  -normalize   Normalize scores to [0, 1]\n"
              << "  -verify      Run correctness verification tests\n"
              << "  -benchmark   Run performance benchmarks\n"
              << "  -generate <n> <d>  Generate random graph (n vertices, density d)\n"
              << "\nInput format:\n"
              << "  Lines starting with # or %% are comments.\n"
              << "  Each edge line: u v [weight]\n"
              << "\nExamples:\n"
              << "  " << prog << " graph.txt\n"
              << "  " << prog << " -w -1 -top 10 -o results.txt graph.txt\n"
              << "  " << prog << " -u -top 20 -o results.txt graph.txt\n"
              << "  " << prog << " -verify\n"
              << "  " << prog << " -benchmark\n"
              << "  " << prog << " -generate 1000 0.05\n";
}

// ---------------------------------------------------------------------------
//  Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Parse command-line arguments
    bool directed = false;
    bool weighted = false;
    bool one_indexed = false;
    bool normalize = false;
    bool do_verify = false;
    bool do_benchmark = false;
    bool do_generate = false;
    int top_k = 0;
    int gen_n = 0;
    double gen_density = 0.0;
    std::string input_file;
    std::string output_file;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d") directed = true;
        else if (arg == "-u") directed = false;
        else if (arg == "-w") weighted = true;
        else if (arg == "-1") one_indexed = true;
        else if (arg == "-normalize") normalize = true;
        else if (arg == "-verify") do_verify = true;
        else if (arg == "-benchmark") do_benchmark = true;
        else if (arg == "-o" && i + 1 < argc) output_file = argv[++i];
        else if (arg == "-top" && i + 1 < argc) top_k = std::atoi(argv[++i]);
        else if (arg == "-generate" && i + 2 < argc) {
            do_generate = true;
            gen_n = std::atoi(argv[++i]);
            gen_density = std::atof(argv[++i]);
        }
        else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg[0] != '-') {
            input_file = arg;
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Handle special modes
    if (do_verify) {
        return verify_small() ? 0 : 1;
    }

    if (do_benchmark) {
        verify_small(true);
        run_benchmark();
        return 0;
    }

    if (do_generate) {
        std::cerr << "Generating random graph (n=" << gen_n
                  << ", density=" << gen_density << ")...\n";
        Graph G = generate_erdos_renyi(gen_n, gen_density, directed, weighted);
        std::cerr << "Graph: " << G.n << " vertices, " << (directed ? G.m : G.m / 2)
                  << " edges\n\n";

        std::cerr << "Computing betweenness centrality...\n";
        auto t0 = std::chrono::high_resolution_clock::now();
        auto CB = brandes_betweenness(G, true);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        std::cerr << "\n=== Results ===\n";
        std::cerr << "Total time: " << std::fixed << std::setprecision(3) << elapsed << " seconds\n";
        std::cerr << "Vertices:   " << G.n << "\n";
        std::cerr << "Edges:      " << (directed ? G.m : G.m / 2) << "\n\n";

        if (output_file.empty()) {
            print_results(G, CB, normalize, top_k, std::cout);
        } else {
            std::ofstream fout(output_file);
            print_results(G, CB, normalize, top_k, fout);
            std::cerr << "Results written to: " << output_file << "\n";
        }
        return 0;
    }

    // Normal mode: read graph from file
    if (input_file.empty()) {
        std::cerr << "Error: No input file specified.\n\n";
        print_usage(argv[0]);
        return 1;
    }

    size_t mem_before = get_current_rss();

    std::cerr << "Reading graph from '" << input_file << "'...\n";
    Graph G = read_graph(input_file, directed, weighted, one_indexed);
    int display_edges = directed ? G.m : G.m / 2;
    std::cerr << "Graph: " << G.n << " vertices, " << display_edges
              << " edges"
              << (directed ? " (directed)" : " (undirected)")
              << (weighted ? " (weighted)" : " (unweighted)")
              << "\n\n";

    std::cerr << "Computing betweenness centrality using Brandes' algorithm...\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    auto CB = brandes_betweenness(G, true);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    size_t peak_mem = get_peak_rss();
    size_t current_mem = get_current_rss();
    size_t estimated_mem = estimate_algorithm_memory(G.n, G.m);

    // Print structured summary to both stderr and stdout
    auto print_summary = [&](std::ostream& out) {
        out << "\n================================================================\n";
        out << "  BRANDES' BETWEENNESS CENTRALITY — RESULTS\n";
        out << "================================================================\n";
        out << "  Dataset:       " << input_file << "\n";
        out << "  Algorithm:     Brandes' (" << (weighted ? "Dijkstra" : "BFS") << " variant)\n";
        out << "  Graph type:    " << (directed ? "Directed" : "Undirected")
            << ", " << (weighted ? "Weighted" : "Unweighted") << "\n";
        out << "  Vertices (n):  " << G.n << "\n";
        out << "  Edges (m):     " << display_edges << "\n";
        out << "  ---------------------------------------------------------------\n";
        out << "  Execution time:          " << std::fixed << std::setprecision(3) << elapsed << " seconds\n";
        out << "  Estimated space (algo):  " << format_bytes(estimated_mem) << "\n";
        if (peak_mem > 0)
            out << "  Peak RSS (measured):     " << format_bytes(peak_mem) << "\n";
        if (current_mem > 0)
            out << "  Current RSS (measured):  " << format_bytes(current_mem) << "\n";
        out << "  Time complexity:         O(n*m) = O(" << G.n << " * " << display_edges
            << ") = O(" << (long long)G.n * display_edges << ")\n";
        out << "  Space complexity:        O(n + m) = O(" << G.n << " + " << display_edges << ")\n";
        out << "================================================================\n\n";
    };

    print_summary(std::cerr);

    // Output results
    if (output_file.empty()) {
        print_summary(std::cout);
        print_results(G, CB, normalize, top_k, std::cout);
    } else {
        std::ofstream fout(output_file);
        print_summary(fout);
        print_results(G, CB, normalize, top_k, fout);
        std::cerr << "Results written to: " << output_file << "\n";
    }

    return 0;
}
