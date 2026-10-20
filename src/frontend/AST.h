#pragma once

#include "common/SourceLoc.h"
#include "common/Types.h"
#include <string>
#include <vector>
#include <memory>

namespace kernelforge {

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    Shl, Shr,
    And, Or, Xor,
    LAnd, LOr,
    Eq, Ne, Lt, Le, Gt, Ge,
    Assign,
    AddAssign, SubAssign, MulAssign, DivAssign
};

enum class UnaryOp {
    Neg,
    Not,
    BitNot
};

enum class BuiltinKind {
    ThreadIdx,
    BlockIdx,
    BlockDim,
    GridDim
};

class ASTVisitor;

class ASTNode {
public:
    virtual ~ASTNode() = default;
    SourceLoc loc;

    explicit ASTNode(SourceLoc l) : loc(std::move(l)) {}
};

class ExprAST : public ASTNode {
public:
    std::shared_ptr<Type> inferredType;

    explicit ExprAST(SourceLoc l) : ASTNode(std::move(l)) {}
    virtual void accept(ASTVisitor* visitor) = 0;
};

class IntLiteralExprAST : public ExprAST {
public:
    int value;

    IntLiteralExprAST(SourceLoc l, int v)
        : ExprAST(std::move(l)), value(v) {
        inferredType = IntType::get();
    }
    void accept(ASTVisitor* visitor) override;
};

class FloatLiteralExprAST : public ExprAST {
public:
    float value;

    FloatLiteralExprAST(SourceLoc l, float v)
        : ExprAST(std::move(l)), value(v) {
        inferredType = FloatType::get();
    }
    void accept(ASTVisitor* visitor) override;
};

class BoolLiteralExprAST : public ExprAST {
public:
    bool value;

    BoolLiteralExprAST(SourceLoc l, bool v)
        : ExprAST(std::move(l)), value(v) {
        inferredType = BoolType::get();
    }
    void accept(ASTVisitor* visitor) override;
};

class VarExprAST : public ExprAST {
public:
    std::string name;

    VarExprAST(SourceLoc l, std::string n)
        : ExprAST(std::move(l)), name(std::move(n)) {}
    void accept(ASTVisitor* visitor) override;
};

class BuiltinExprAST : public ExprAST {
public:
    BuiltinKind kind;
    char dimension; // 'x', 'y', or 'z'

    BuiltinExprAST(SourceLoc l, BuiltinKind k, char dim)
        : ExprAST(std::move(l)), kind(k), dimension(dim) {
        inferredType = IntType::get();
    }
    void accept(ASTVisitor* visitor) override;
};

class BinaryExprAST : public ExprAST {
public:
    BinaryOp op;
    std::unique_ptr<ExprAST> lhs;
    std::unique_ptr<ExprAST> rhs;

    BinaryExprAST(SourceLoc l, BinaryOp o, std::unique_ptr<ExprAST> l_expr, std::unique_ptr<ExprAST> r_expr)
        : ExprAST(std::move(l)), op(o), lhs(std::move(l_expr)), rhs(std::move(r_expr)) {}
    void accept(ASTVisitor* visitor) override;
};

class UnaryExprAST : public ExprAST {
public:
    UnaryOp op;
    std::unique_ptr<ExprAST> operand;

    UnaryExprAST(SourceLoc l, UnaryOp o, std::unique_ptr<ExprAST> expr)
        : ExprAST(std::move(l)), op(o), operand(std::move(expr)) {}
    void accept(ASTVisitor* visitor) override;
};

class IndexExprAST : public ExprAST {
public:
    std::unique_ptr<ExprAST> base;
    std::unique_ptr<ExprAST> index;

    IndexExprAST(SourceLoc l, std::unique_ptr<ExprAST> b, std::unique_ptr<ExprAST> idx)
        : ExprAST(std::move(l)), base(std::move(b)), index(std::move(idx)) {}
    void accept(ASTVisitor* visitor) override;
};

class CallExprAST : public ExprAST {
public:
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;

    CallExprAST(SourceLoc l, std::string name, std::vector<std::unique_ptr<ExprAST>> a)
        : ExprAST(std::move(l)), callee(std::move(name)), args(std::move(a)) {}
    void accept(ASTVisitor* visitor) override;
};

class CastExprAST : public ExprAST {
public:
    std::shared_ptr<Type> targetType;
    std::unique_ptr<ExprAST> expr;

    CastExprAST(SourceLoc l, std::shared_ptr<Type> t, std::unique_ptr<ExprAST> e)
        : ExprAST(std::move(l)), targetType(std::move(t)), expr(std::move(e)) {
        inferredType = targetType;
    }
    void accept(ASTVisitor* visitor) override;
};

// Statements
class StmtAST : public ASTNode {
public:
    explicit StmtAST(SourceLoc l) : ASTNode(std::move(l)) {}
    virtual void accept(ASTVisitor* visitor) = 0;
};

class ExprStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> expr;

    ExprStmtAST(SourceLoc l, std::unique_ptr<ExprAST> e)
        : StmtAST(std::move(l)), expr(std::move(e)) {}
    void accept(ASTVisitor* visitor) override;
};

class VarDeclStmtAST : public StmtAST {
public:
    std::shared_ptr<Type> type;
    std::string name;
    std::unique_ptr<ExprAST> init;
    AddressSpace addressSpace;
    size_t arraySize = 0; // if array

    VarDeclStmtAST(SourceLoc l, std::shared_ptr<Type> t, std::string n,
                   std::unique_ptr<ExprAST> i = nullptr,
                   AddressSpace space = AddressSpace::Local, size_t arrSz = 0)
        : StmtAST(std::move(l)), type(std::move(t)), name(std::move(n)),
          init(std::move(i)), addressSpace(space), arraySize(arrSz) {}
    void accept(ASTVisitor* visitor) override;
};

class BlockStmtAST : public StmtAST {
public:
    std::vector<std::unique_ptr<StmtAST>> statements;

    BlockStmtAST(SourceLoc l, std::vector<std::unique_ptr<StmtAST>> stmts = {})
        : StmtAST(std::move(l)), statements(std::move(stmts)) {}
    void accept(ASTVisitor* visitor) override;
};

class IfStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<StmtAST> thenStmt;
    std::unique_ptr<StmtAST> elseStmt; // can be null

    IfStmtAST(SourceLoc l, std::unique_ptr<ExprAST> c,
              std::unique_ptr<StmtAST> t, std::unique_ptr<StmtAST> e = nullptr)
        : StmtAST(std::move(l)), cond(std::move(c)), thenStmt(std::move(t)), elseStmt(std::move(e)) {}
    void accept(ASTVisitor* visitor) override;
};

class WhileStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<StmtAST> body;

    WhileStmtAST(SourceLoc l, std::unique_ptr<ExprAST> c, std::unique_ptr<StmtAST> b)
        : StmtAST(std::move(l)), cond(std::move(c)), body(std::move(b)) {}
    void accept(ASTVisitor* visitor) override;
};

class ForStmtAST : public StmtAST {
public:
    std::unique_ptr<StmtAST> init; // can be VarDeclStmtAST or ExprStmtAST or null
    std::unique_ptr<ExprAST> cond; // can be null
    std::unique_ptr<ExprAST> step; // can be null
    std::unique_ptr<StmtAST> body;

    ForStmtAST(SourceLoc l, std::unique_ptr<StmtAST> i, std::unique_ptr<ExprAST> c,
               std::unique_ptr<ExprAST> s, std::unique_ptr<StmtAST> b)
        : StmtAST(std::move(l)), init(std::move(i)), cond(std::move(c)),
          step(std::move(s)), body(std::move(b)) {}
    void accept(ASTVisitor* visitor) override;
};

class ReturnStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> value; // can be null

    ReturnStmtAST(SourceLoc l, std::unique_ptr<ExprAST> v = nullptr)
        : StmtAST(std::move(l)), value(std::move(v)) {}
    void accept(ASTVisitor* visitor) override;
};

class SyncStmtAST : public StmtAST {
public:
    SyncStmtAST(SourceLoc l) : StmtAST(std::move(l)) {}
    void accept(ASTVisitor* visitor) override;
};

// Declarations
struct ParamDecl {
    SourceLoc loc;
    std::shared_ptr<Type> type;
    std::string name;
};

class FunctionDeclAST : public ASTNode {
public:
    bool isKernel;
    std::shared_ptr<Type> returnType;
    std::string name;
    std::vector<ParamDecl> params;
    std::unique_ptr<BlockStmtAST> body; // null if forward declaration

    FunctionDeclAST(SourceLoc l, bool kernel, std::shared_ptr<Type> retType,
                    std::string n, std::vector<ParamDecl> p,
                    std::unique_ptr<BlockStmtAST> b)
        : ASTNode(std::move(l)), isKernel(kernel), returnType(std::move(retType)),
          name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
    void accept(ASTVisitor* visitor);
};

class GlobalDeclAST : public ASTNode {
public:
    std::shared_ptr<Type> type;
    std::string name;
    AddressSpace addressSpace;
    size_t arraySize = 0;

    GlobalDeclAST(SourceLoc l, std::shared_ptr<Type> t, std::string n,
                  AddressSpace space, size_t sz = 0)
        : ASTNode(std::move(l)), type(std::move(t)), name(std::move(n)),
          addressSpace(space), arraySize(sz) {}
    void accept(ASTVisitor* visitor);
};

class ProgramAST : public ASTNode {
public:
    std::vector<std::unique_ptr<GlobalDeclAST>> globals;
    std::vector<std::unique_ptr<FunctionDeclAST>> functions;

    explicit ProgramAST(SourceLoc l) : ASTNode(std::move(l)) {}
    void accept(ASTVisitor* visitor);
};

// Visitor Pattern
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(IntLiteralExprAST* node) = 0;
    virtual void visit(FloatLiteralExprAST* node) = 0;
    virtual void visit(BoolLiteralExprAST* node) = 0;
    virtual void visit(VarExprAST* node) = 0;
    virtual void visit(BuiltinExprAST* node) = 0;
    virtual void visit(BinaryExprAST* node) = 0;
    virtual void visit(UnaryExprAST* node) = 0;
    virtual void visit(IndexExprAST* node) = 0;
    virtual void visit(CallExprAST* node) = 0;
    virtual void visit(CastExprAST* node) = 0;

    virtual void visit(ExprStmtAST* node) = 0;
    virtual void visit(VarDeclStmtAST* node) = 0;
    virtual void visit(BlockStmtAST* node) = 0;
    virtual void visit(IfStmtAST* node) = 0;
    virtual void visit(WhileStmtAST* node) = 0;
    virtual void visit(ForStmtAST* node) = 0;
    virtual void visit(ReturnStmtAST* node) = 0;
    virtual void visit(SyncStmtAST* node) = 0;

    virtual void visit(FunctionDeclAST* node) = 0;
    virtual void visit(GlobalDeclAST* node) = 0;
    virtual void visit(ProgramAST* node) = 0;
};

inline void IntLiteralExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void FloatLiteralExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void BoolLiteralExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void VarExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void BuiltinExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void BinaryExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void UnaryExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void IndexExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void CallExprAST::accept(ASTVisitor* v) { v->visit(this); }
inline void CastExprAST::accept(ASTVisitor* v) { v->visit(this); }

inline void ExprStmtAST::accept(ASTVisitor* v) { v->visit(this); }
inline void VarDeclStmtAST::accept(ASTVisitor* v) { v->visit(this); }
inline void BlockStmtAST::accept(ASTVisitor* v) { v->visit(this); }
inline void IfStmtAST::accept(ASTVisitor* v) { v->visit(this); }
inline void WhileStmtAST::accept(ASTVisitor* v) { v->visit(this); }
inline void ForStmtAST::accept(ASTVisitor* v) { v->visit(this); }
inline void ReturnStmtAST::accept(ASTVisitor* v) { v->visit(this); }
inline void SyncStmtAST::accept(ASTVisitor* v) { v->visit(this); }

inline void FunctionDeclAST::accept(ASTVisitor* v) { v->visit(this); }
inline void GlobalDeclAST::accept(ASTVisitor* v) { v->visit(this); }
inline void ProgramAST::accept(ASTVisitor* v) { v->visit(this); }

} // namespace kernelforge
