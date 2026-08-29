// clang-tidy as build-graph nodes, one edge per file.
//
// WHAT THE ENGINE ALREADY DOES, AND THIS DOES NOT REPEAT: mcpp writes every
// action's full argv into build.ninja, recoverable with `ninja -t commands`.
// A second copy of that would only drift. What this rule owns is the other
// half -- WHICH KNOBS produced the command -- and it goes in each edge's
// description.
export module clangtidy;

import std;
import mcpp;

export namespace clangtidy {

struct options {
    // Where the stamps go. One per checked file; mcpp creates them when the
    // command succeeds, so the analyser never has to know they exist.
    std::string out_dir = std::string(mcpp::out_dir()) + "/clang-tidy";
    // clang-tidy's `-p`. A closed engine variable, expanded by mcpp, so the
    // rule never has to know the build directory's layout.
    bool compile_db = true;
    // Gate compilation on the result instead of running beside it. Costs the
    // whole build's parallelism, and buys nothing unless a failure means the
    // compile was wasted anyway.
    bool blocking = false;
    // Extra argv passed through verbatim, e.g. {"--checks=-*,bugprone-*"}.
    std::vector<std::string> args;
    // ⚠️ ON BY DEFAULT, AND THE RULE IS DECORATION WITHOUT IT.
    //
    // A check's contract is that the EXIT CODE is the verdict. Measured:
    // clang-tidy exits 0 while printing a diagnostic —
    //
    //   main.cpp:6:5: warning: the result from calling 'memcpy' is not
    //   null-terminated [bugprone-not-null-terminated-result]
    //   clang-tidy exit=0
    //
    // — so a rule that simply runs it produces a check that passes on every
    // input it will ever see, and a build that reports success while the
    // finding scrolls past. That is precisely the trap a rule package exists
    // to take off every consumer.
    //
    // Turn it off only to make the checks advisory on purpose, and know that
    // the edge then cannot fail.
    bool warnings_are_errors = true;
    // The executable. Empty asks the consumer's dependency graph for it, which
    // is the spelling that keeps the version under the consumer's control.
    std::string program;
};

// One planned edge, handed back so a caller who needs to adjust it can.
// Without this pair the rule has a cliff: past its last knob the only way out
// is to hand-write the action, and that copy then drifts away from the rule.
struct edge {
    std::string              id;
    std::string              description;
    std::vector<std::string> command;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    bool                     blocking = false;
};

inline std::vector<edge> plan(std::span<const std::string> files,
                              options opt = {})
{
    const std::string root = mcpp::manifest_dir();
    if (root.empty()) {
        std::println(std::cerr,
            "clangtidy: no mcpp build context -- this runs from build.mcpp");
        return {};
    }
    if (files.empty()) {
        std::println(std::cerr, "clangtidy: no files to check");
        return {};
    }

    std::string exe = opt.program;
    if (exe.empty()) {
        const char* p = mcpp::dep_bin("llvm", "clang-tidy");
        if (!p || !*p) {
            std::println(std::cerr,
                "clangtidy: no clang-tidy. Declare the package that provides "
                "it, so its version is yours to pin:\n"
                "  [build-dependencies]\n"
                "  llvm = {{ version = \"...\", tools = [\"clang-tidy\"] }}\n"
                "  (or set options::program to an absolute path)");
            return {};
        }
        exe = p;
    }

    std::vector<edge> out;
    for (auto const& f : files) {
        const std::string stem  = std::filesystem::path(f).stem().string();
        const std::string stamp = opt.out_dir + "/" + stem + ".stamp";
        std::vector<std::string> cmd{ exe };
        if (opt.warnings_are_errors) cmd.push_back("--warnings-as-errors=*");
        for (auto const& a : opt.args) cmd.push_back(a);
        if (opt.compile_db) { cmd.push_back("-p"); cmd.push_back("${mcpp.compile_db}"); }
        cmd.push_back(root + "/" + f);

        std::string knobs = opt.blocking ? "blocking" : "parallel";
        if (!opt.args.empty()) knobs += ", " + std::to_string(opt.args.size()) + " extra arg(s)";
        out.push_back(edge{
            .id          = "clang-tidy:" + stem,
            .description = "clang-tidy " + f + " (" + knobs + ")",
            .command     = std::move(cmd),
            .inputs      = { root + "/" + f },
            .outputs     = { stamp },
            .blocking    = opt.blocking,
        });
    }
    return out;
}

// Hand the planned edges to mcpp. Split from `plan` so a caller can edit the
// list in between; `check` below is exactly the composition of the two.
inline bool submit(std::span<const edge> edges) {
    if (edges.empty()) return false;
    for (auto const& e : edges) {
        mcpp::action a;
        a.id          = e.id.c_str();
        a.role        = "check";
        a.description = e.description.c_str();
        a.blocking    = e.blocking;
        for (auto const& c : e.command) a.arg(c.c_str());
        for (auto const& i : e.inputs)  a.input(i.c_str());
        for (auto const& o : e.outputs) a.output(o.c_str());
        a.submit();
    }
    return true;
}

inline bool check(std::span<const std::string> files, options opt = {}) {
    auto edges = plan(files, std::move(opt));
    return submit(edges);
}

} // namespace clangtidy
