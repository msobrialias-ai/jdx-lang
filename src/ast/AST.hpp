#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "lexer/Lexer.hpp"

namespace jdx::ast {

struct Expr;
struct Stmt;
struct BlockStmt;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using BlockPtr = std::shared_ptr<BlockStmt>;

struct Expr {
    lexer::Token token;
    explicit Expr(lexer::Token t) : token(std::move(t)) {}
    virtual ~Expr() = default;
};

struct LiteralExpr final : Expr {
    lexer::Token literalToken;
    explicit LiteralExpr(lexer::Token t) : Expr(t), literalToken(std::move(t)) {}
};

struct VariableExpr final : Expr {
    std::string name;
    VariableExpr(lexer::Token t, std::string n) : Expr(t), name(std::move(n)) {}
};

struct AssignExpr final : Expr {
    std::string name;
    ExprPtr value;
    AssignExpr(lexer::Token t, std::string n, ExprPtr v) : Expr(t), name(std::move(n)), value(std::move(v)) {}
};

struct UnaryExpr final : Expr {
    lexer::Token op;
    ExprPtr right;
    UnaryExpr(lexer::Token t, ExprPtr r) : Expr(t), op(std::move(t)), right(std::move(r)) {}
};

struct BinaryExpr final : Expr {
    ExprPtr left;
    lexer::Token op;
    ExprPtr right;
    BinaryExpr(ExprPtr l, lexer::Token t, ExprPtr r) : Expr(t), left(std::move(l)), op(std::move(t)), right(std::move(r)) {}
};

struct GroupingExpr final : Expr {
    ExprPtr expression;
    GroupingExpr(lexer::Token t, ExprPtr e) : Expr(t), expression(std::move(e)) {}
};

struct CallExpr final : Expr {
    ExprPtr callee;
    lexer::Token paren;
    std::vector<ExprPtr> arguments;
    CallExpr(ExprPtr c, lexer::Token p, std::vector<ExprPtr> a) : Expr(p), callee(std::move(c)), paren(std::move(p)), arguments(std::move(a)) {}
};

struct GetExpr final : Expr {
    ExprPtr object;
    std::string name;
    GetExpr(ExprPtr o, lexer::Token t, std::string n) : Expr(t), object(std::move(o)), name(std::move(n)) {}
};

struct SetExpr final : Expr {
    ExprPtr object;
    std::string name;
    ExprPtr value;
    SetExpr(ExprPtr o, lexer::Token t, std::string n, ExprPtr v) : Expr(t), object(std::move(o)), name(std::move(n)), value(std::move(v)) {}
};

struct ImportExpr final : Expr {
    ExprPtr path;
    ImportExpr(lexer::Token t, ExprPtr p) : Expr(t), path(std::move(p)) {}
};

struct ImportBinding {
    std::string imported;
    std::string local;
};

struct ExportBinding {
    std::string local;
    std::string exported;
};

struct Stmt {
    lexer::Token token;
    explicit Stmt(lexer::Token t) : token(std::move(t)) {}
    virtual ~Stmt() = default;
};

struct ExprStmt final : Stmt {
    ExprPtr expression;
    ExprStmt(lexer::Token t, ExprPtr e) : Stmt(t), expression(std::move(e)) {}
};

struct VarStmt final : Stmt {
    bool isConst {false};
    bool isExported {false};
    std::string name;
    ExprPtr initializer;
    VarStmt(lexer::Token t, bool c, bool e, std::string n, ExprPtr i) : Stmt(t), isConst(c), isExported(e), name(std::move(n)), initializer(std::move(i)) {}
};

struct ReturnStmt final : Stmt {
    ExprPtr value;
    ReturnStmt(lexer::Token t, ExprPtr v) : Stmt(t), value(std::move(v)) {}
};

struct BreakStmt final : Stmt { using Stmt::Stmt; };
struct ContinueStmt final : Stmt { using Stmt::Stmt; };

struct IfStmt final : Stmt {
    ExprPtr condition;
    StmtPtr thenBranch;
    std::vector<std::pair<ExprPtr, StmtPtr>> elseIfBranches;
    StmtPtr elseBranch;
    IfStmt(lexer::Token t, ExprPtr c, StmtPtr thenB) : Stmt(t), condition(std::move(c)), thenBranch(std::move(thenB)) {}
};

struct WhileStmt final : Stmt {
    ExprPtr condition;
    StmtPtr body;
    WhileStmt(lexer::Token t, ExprPtr c, StmtPtr b) : Stmt(t), condition(std::move(c)), body(std::move(b)) {}
};

struct ForStmt final : Stmt {
    StmtPtr initializer;
    ExprPtr condition;
    ExprPtr increment;
    StmtPtr body;
    ForStmt(lexer::Token t, StmtPtr i, ExprPtr c, ExprPtr inc, StmtPtr b)
        : Stmt(t), initializer(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
};

struct BlockStmt final : Stmt {
    std::vector<StmtPtr> statements;
    explicit BlockStmt(lexer::Token t) : Stmt(t) {}
};

struct FunctionStmt final : Stmt {
    std::string name;
    std::vector<std::string> params;
    BlockPtr body;
    bool isExported {false};
    bool isDefaultExport {false};
    FunctionStmt(lexer::Token t, std::string n, std::vector<std::string> p, BlockPtr b, bool e, bool d)
        : Stmt(t), name(std::move(n)), params(std::move(p)), body(std::move(b)), isExported(e), isDefaultExport(d) {}
};

struct ImportStmt final : Stmt {
    std::string source;
    bool sideEffectOnly {false};
    std::string defaultLocal;
    std::string namespaceLocal;
    std::vector<ImportBinding> named;
    ImportStmt(lexer::Token t,
               std::string s,
               bool sideEffect,
               std::string defaultBinding,
               std::string namespaceBinding,
               std::vector<ImportBinding> namedBindings)
        : Stmt(t),
          source(std::move(s)),
          sideEffectOnly(sideEffect),
          defaultLocal(std::move(defaultBinding)),
          namespaceLocal(std::move(namespaceBinding)),
          named(std::move(namedBindings)) {}
};

struct ExportListStmt final : Stmt {
    std::vector<ExportBinding> bindings;
    explicit ExportListStmt(lexer::Token t, std::vector<ExportBinding> b)
        : Stmt(t), bindings(std::move(b)) {}
};

struct ExportDefaultStmt final : Stmt {
    ExprPtr value;
    ExportDefaultStmt(lexer::Token t, ExprPtr v) : Stmt(t), value(std::move(v)) {}
};

struct Program {
    std::vector<StmtPtr> statements;
};

} // namespace jdx::ast
