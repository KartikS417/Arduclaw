#pragma once
#include <vector>
#include <functional>

struct Rule {
    std::function<bool()> condition;
    std::function<void()> action;
};

class RuleEngine {
private:
    std::vector<Rule> rules;

public:
    void addRule(std::function<bool()> cond,
                 std::function<void()> act) {
        rules.push_back({cond, act});
    }

    void evaluate() {
        for (auto &r : rules) {
            if (r.condition()) r.action();
        }
    }
};
