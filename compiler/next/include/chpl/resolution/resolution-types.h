/*
 * Copyright 2021 Hewlett Packard Enterprise Development LP
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CHPL_RESOLUTION_RESOLUTION_TYPES_H
#define CHPL_RESOLUTION_RESOLUTION_TYPES_H

#include "chpl/resolution/scope-types.h"
#include "chpl/types/QualifiedType.h"
#include "chpl/types/Type.h"
#include "chpl/uast/ASTNode.h"
#include "chpl/uast/Function.h"
#include "chpl/util/memory.h"

#include <unordered_map>
#include <utility>

namespace chpl {
namespace resolution {

// TODO: Should some/all of these structs be classes
// with getters etc? That would be appropriate for
// use as part of the library API.

/**
  An untyped function signature. This is really just the part of a function
  including the formals. It exists so that the process of identifying
  candidates does not need to depend on the bodies of the function
  (in terms of incremental recomputation).
 */
struct UntypedFnSignature {
  ID functionId;
  UniqueString name;
  bool isMethod; // in that case, formals[0] is the receiver
  uast::Function::Kind kind;
  std::vector<const uast::Decl*> formals;
  const uast::Expression* whereClause;

  UntypedFnSignature(const uast::Function* fn)
    : functionId(fn->id()),
      name(fn->name()),
      isMethod(fn->isMethod()),
      kind(fn->kind()),
      whereClause(fn->whereClause()) {
    for (auto formal : fn->formals()) {
      formals.push_back(formal);
    }
  }

  bool operator==(const UntypedFnSignature& other) const {
    return functionId == other.functionId &&
           name == other.name &&
           isMethod == other.isMethod &&
           kind == other.kind &&
           formals == other.formals &&
           whereClause == other.whereClause;
  }
  bool operator!=(const UntypedFnSignature& other) const {
    return !(*this == other);
  }
};

using SubstitutionsMap = std::unordered_map<const uast::Decl*, types::QualifiedType>;

struct CallInfoActual {
  types::QualifiedType type;
  UniqueString byName;

  bool operator==(const CallInfoActual& other) const {
    return type == other.type &&
           byName == other.byName;
  }
  bool operator!=(const CallInfoActual& other) const {
    return !(*this == other);
  }
  size_t hash() const {
    size_t h1 = chpl::hash(type);
    size_t h2 = chpl::hash(byName);
    size_t ret = 0;
    ret = hash_combine(ret, h1);
    ret = hash_combine(ret, h2);
    return ret;
  }
};

struct CallInfo {
  UniqueString name;                   // the name of the called thing
  bool isMethod = false;               // in that case, actuals[0] is receiver
  std::vector<CallInfoActual> actuals; // types/params/names of actuals 

  bool operator==(const CallInfo& other) const {
    return name == other.name &&
           isMethod == other.isMethod &&
           actuals == other.actuals;
  }
  bool operator!=(const CallInfo& other) const {
    return !(*this == other);
  }
  size_t hash() const {
    size_t h1 = chpl::hash(name);
    size_t h2 = isMethod;
    size_t h3 = chpl::hash(actuals);
    size_t ret = 0;
    ret = hash_combine(ret, h1);
    ret = hash_combine(ret, h2);
    ret = hash_combine(ret, h3);
    return ret;
  }
};


/**
  Contains information about symbols available from point-of-instantiation
  in order to implement caching of instantiations.
 */
struct PoiInfo {
  // is this PoiInfo for a function that has been resolved, or
  // for a function we are about to resolve?
  bool resolved = false;

  // For a not-yet-resolved instantiation
  const PoiScope* poiScope = nullptr;

  // TODO: add VisibilityInfo etc -- names of calls.
  // see PR #16261

  // For a resolved instantiation

  // Tracking how calls using POI were resolved.
  // This is a set of pairs of (Call ID, Function ID).
  // This includes POI calls from functions called in this Function,
  // transitively
  std::set<std::pair<ID, ID>> poiFnIdsUsed;

  // default construct a PoiInfo
  PoiInfo() { }

  // construct a PoiInfo for a not-yet-resolved instantiation
  PoiInfo(const PoiScope* poiScope)
    : resolved(false), poiScope(poiScope) {
  }
  // construct a PoiInfo for a resolved instantiation
  PoiInfo(std::set<std::pair<ID, ID>> poiFnIdsUsed)
    : resolved(true), poiFnIdsUsed(std::move(poiFnIdsUsed)) {
  }

  // return true if the two passed PoiInfos represent the same information
  // (for use in an update function)
  static bool updateEquals(const PoiInfo& a, const PoiInfo& b) {
    return a.resolved == b.resolved &&
           a.poiScope == b.poiScope &&
           a.poiFnIdsUsed == b.poiFnIdsUsed;
  }

  void swap(PoiInfo& other) {
    std::swap(resolved, other.resolved);
    std::swap(poiScope, other.poiScope);
    poiFnIdsUsed.swap(other.poiFnIdsUsed);
  }

  // accumulate PoiInfo from a call into this PoiInfo
  void accumulate(const PoiInfo& addPoiInfo);

  // return true if 'this' represents a resolved function that can
  // be reused given the PoiInfo for a not-yet-resolved function in 'check'.
  bool canReuse(const PoiInfo& check) const;

  // return true if one of the PoiInfos is a resolved function that
  // can be reused given PoiInfo for a not-yet-resolved function.
  static bool reuseEquals(const PoiInfo& a, const PoiInfo& b) {
    if (a.resolved && !b.resolved) {
      return a.canReuse(b);
    }
    if (b.resolved && !a.resolved) {
      return b.canReuse(a);
    }
    return updateEquals(a, b);
  }

  // hashing a PoiInfo gives 0 always
  // (instead we rely on == in the hashtable so that we can
  //  apply canReuse to figure out if an instantiation can be reused).
  size_t hash() const {
    return 0;
  }
  // == and != for the hashtable
  bool operator==(const PoiInfo& other) const {
    return PoiInfo::reuseEquals(*this, other);
  }
  bool operator!=(const PoiInfo& other) const {
    return !(*this == other);
  }
};

// TODO: should this actually be types::FunctionType?
/**
  This represents a typed function signature.
*/
struct TypedFnSignature {
  typedef enum {
    WHERE_NONE,  // no where clause
    WHERE_TBD,   // where clause not resolved yet
    WHERE_TRUE,  // where resulted in true
    WHERE_FALSE, // where resulted in false
  } WhereClauseResult;

  // What is the untyped function signature?
  const UntypedFnSignature* untypedSignature;
  // What is the type of each of the formals?
  std::vector<types::QualifiedType> formalTypes;
  // If there was a where clause, what was the result of evaluating it?
  WhereClauseResult whereClauseResult = WHERE_TBD;

  // Are any of the formals generic or unknown at this point?
  bool needsInstantiation = true;

  // Is this TypedFnSignature representing an instantiation?
  // If so, what is the generic TypedFnSignature that was instantiated?
  const TypedFnSignature* instantiatedFrom = nullptr;

  // Is this for an inner Function? If so, what is the parent
  // function signature?
  const TypedFnSignature* parentFn = nullptr;

  // TODO: This could include a substitutions map, if we need it.
  // The formalTypes above might be enough, though.

  bool operator==(const TypedFnSignature& other) const {
    return untypedSignature == other.untypedSignature &&
           formalTypes == other.formalTypes &&
           whereClauseResult == other.whereClauseResult &&
           needsInstantiation == other.needsInstantiation &&
           instantiatedFrom == other.instantiatedFrom &&
           parentFn == other.parentFn;
  }
  bool operator!=(const TypedFnSignature& other) const {
    return !(*this == other);
  }

  std::string toString() const;

  const ID& functionId() const {
    return untypedSignature->functionId;
  }
};

/**
  Stores the most specific candidates when resolving a function call.
*/
class MostSpecificCandidates {
 public:
  typedef enum {
    // the slots in the candidates array for return intent
    // overloading
    REF = 0,
    CONST_REF,
    VALUE,
    // NUM_INTENTS is the size of the candidates array
    NUM_INTENTS,
  } Intent;

 private:
  const TypedFnSignature* candidates[NUM_INTENTS] = {nullptr};

 public:
  const TypedFnSignature* const* begin() const {
    return &candidates[0];
  }
  const TypedFnSignature* const* end() const {
    return &candidates[NUM_INTENTS];
  }

  void setBestRef(const TypedFnSignature* sig) {
    candidates[REF] = sig;
  }
  void setBestConstRef(const TypedFnSignature* sig) {
    candidates[CONST_REF] = sig;
  }
  void setBestValue(const TypedFnSignature* sig) {
    candidates[VALUE] = sig;
  }

  const TypedFnSignature* bestRef() const {
    return candidates[REF];
  }
  const TypedFnSignature* bestConstRef() const {
    return candidates[CONST_REF];
  }
  const TypedFnSignature* bestValue() const {
    return candidates[VALUE];
  }

  /**
    If there is exactly one candidate, return that candidate.
    Otherwise, return nullptr.
   */
  const TypedFnSignature* only() const {
    const TypedFnSignature* ret = nullptr;
    int nPresent = 0;
    for (int i = 0; i < NUM_INTENTS; i++) {
      const TypedFnSignature* sig = candidates[i];
      if (sig != nullptr) {
        ret = sig;
        nPresent++;
      }
    }
    if (nPresent != 1) {
      return nullptr;
    }
    return ret;
  }

  bool operator==(const MostSpecificCandidates& other) const {
    for (int i = 0; i < NUM_INTENTS; i++) {
      if (candidates[i] != other.candidates[i])
        return false;
    }
    return true;
  }
  bool operator!=(const MostSpecificCandidates& other) const {
    return !(*this == other);
  }
  void swap(MostSpecificCandidates& other) {
    for (int i = 0; i < NUM_INTENTS; i++) {
      std::swap(candidates[i], other.candidates[i]);
    }
  }
};

struct CallResolutionResult {
  // what are the candidates for return-intent overloading?
  MostSpecificCandidates mostSpecific;
  // what is the type of the call expression?
  types::QualifiedType exprType;
  // if any of the candidates were instantiated, what point-of-instantiation
  // scopes were used when resolving their signature or body?
  PoiInfo poiInfo;

  CallResolutionResult(MostSpecificCandidates mostSpecific,
                       types::QualifiedType exprType,
                       PoiInfo poiInfo)
    : mostSpecific(std::move(mostSpecific)),
      exprType(std::move(exprType)),
      poiInfo(std::move(poiInfo)) {
  }

  bool operator==(const CallResolutionResult& other) const {
    return mostSpecific == other.mostSpecific &&
           exprType == other.exprType &&
           PoiInfo::updateEquals(poiInfo, other.poiInfo);
  }
  bool operator!=(const CallResolutionResult& other) const {
    return !(*this == other);
  }
  void swap(CallResolutionResult& other) {
    mostSpecific.swap(other.mostSpecific);
    exprType.swap(other.exprType);
    poiInfo.swap(other.poiInfo);
  }
};

/**
  This type represents a resolved expression.
*/
struct ResolvedExpression {
  // What is its type and param value?
  types::QualifiedType type;
  // For simple (non-function Identifier) cases,
  // the ID of a NamedDecl it refers to
  ID toId;

  // For a function call, what is the most specific candidate,
  // or when using return intent overloading, what are the most specific
  // candidates?
  // The choice between these needs to happen
  // later than the main function resolution.
  MostSpecificCandidates mostSpecific;
  // What point of instantiation scope should be used when
  // resolving functions in mostSpecific?
  const PoiScope* poiScope = nullptr;

  ResolvedExpression() { }

  bool operator==(const ResolvedExpression& other) const {
    return type == other.type &&
           toId == other.toId &&
           mostSpecific == other.mostSpecific &&
           poiScope == other.poiScope;
  }
  bool operator!=(const ResolvedExpression& other) const {
    return !(*this == other);
  }
  void swap(ResolvedExpression& other) {
    type.swap(other.type);
    toId.swap(other.toId);
    mostSpecific.swap(other.mostSpecific);
    std::swap(poiScope, other.poiScope);
  }

  std::string toString() const;
};

/**
 This type is a mapping from postOrderId (which is an integer) to
 ResolvedExpression for storing resolution results *within* a symbol.

 Note that an inner Function would not be covered here.
 */
class ResolutionResultByPostorderID {
 private:
  ID symbolId;
  std::vector<ResolvedExpression> vec;

 public:
  /** prepare to resolve the contents of the passed symbol */
  void setupForSymbol(const uast::ASTNode* ast);
  /** prepare to resolve the signature of the passed function */
  void setupForSignature(const uast::Function* func);
  /** prepare to resolve the body of the passed function */
  void setupForFunction(const uast::Function* func);

  ResolvedExpression& byIdExpanding(const ID& id) {
    auto postorder = id.postOrderId();
    assert(id.symbolPath() == symbolId.symbolPath());
    assert(0 <= postorder);
    if ((size_t) postorder < vec.size()) {
      // OK
    } else {
      vec.resize(postorder+1);
    }
    return vec[postorder];
  }
  ResolvedExpression& byAstExpanding(const uast::ASTNode* ast) {
    return byIdExpanding(ast->id());
  }
  ResolvedExpression& byId(const ID& id) {
    auto postorder = id.postOrderId();
    assert(id.symbolPath() == symbolId.symbolPath());
    assert(0 <= postorder && (size_t) postorder < vec.size());
    return vec[postorder];
  }
  const ResolvedExpression& byId(const ID& id) const {
    auto postorder = id.postOrderId();
    assert(0 <= postorder && (size_t) postorder < vec.size());
    return vec[postorder];
  }
  ResolvedExpression& byAst(const uast::ASTNode* ast) {
    return byId(ast->id());
  }
  const ResolvedExpression& byAst(const uast::ASTNode* ast) const {
    return byId(ast->id());
  }

  bool operator==(const ResolutionResultByPostorderID& other) const {
    return vec == other.vec;
  }
  bool operator!=(const ResolutionResultByPostorderID& other) const {
    return !(*this == other);
  }
  void swap(ResolutionResultByPostorderID& other) {
    vec.swap(other.vec);
  }

  static bool update(ResolutionResultByPostorderID& keep,
                     ResolutionResultByPostorderID& addin);
};

/**
  This type represents a resolved function.
*/
struct ResolvedFunction {
  const TypedFnSignature* signature = nullptr;

  uast::Function::ReturnIntent returnIntent =
    uast::Function::DEFAULT_RETURN_INTENT;

  // this is the output of the resolution process
  ResolutionResultByPostorderID resolutionById;

  // the set of point-of-instantiation scopes used by the instantiation
  PoiInfo poiInfo;

  bool operator==(const ResolvedFunction& other) const {
    return signature == other.signature &&
           returnIntent == other.returnIntent &&
           resolutionById == other.resolutionById &&
           PoiInfo::updateEquals(poiInfo, other.poiInfo);
  }
  bool operator!=(const ResolvedFunction& other) const {
    return !(*this == other);
  }
  void swap(ResolvedFunction& other) {
    std::swap(signature, other.signature);
    std::swap(returnIntent, other.returnIntent);
    resolutionById.swap(other.resolutionById);
    poiInfo.swap(other.poiInfo);
  }

  const ResolvedExpression& byId(const ID& id) const {
    return resolutionById.byId(id);
  }
  const ResolvedExpression& byAst(const uast::ASTNode* ast) const {
    return resolutionById.byAst(ast);
  }

  const ID& functionId() const {
    return signature->functionId();
  }
};

struct FormalActual {
  const uast::Decl* formal = nullptr;
  types::QualifiedType formalType;
  bool hasActual = false; // == false means uses formal default value
  int actualIdx = -1;
  types::QualifiedType actualType;
};

struct FormalActualMap {
  std::vector<FormalActual> byFormalIdx;
  std::vector<int> actualIdxToFormalIdx;
  bool mappingIsValid = false;
  int failingActualIdx = -1;
  int failingFormalIdx = -1;

  bool computeAlignment(const UntypedFnSignature* untyped,
                        const TypedFnSignature* typed,
                        const CallInfo& call);

  static FormalActualMap build(const UntypedFnSignature* untyped,
                               const CallInfo& call);
  static FormalActualMap build(const TypedFnSignature* typed,
                               const CallInfo& call);
};



} // end namespace resolution


/// \cond DO_NOT_DOCUMENT
template<> struct update<resolution::ResolvedExpression> {
  bool operator()(resolution::ResolvedExpression& keep,
                  resolution::ResolvedExpression& addin) const {
    return defaultUpdate(keep, addin);
  }
};

template<> struct update<resolution::MostSpecificCandidates> {
  bool operator()(resolution::MostSpecificCandidates& keep,
                  resolution::MostSpecificCandidates& addin) const {
    return defaultUpdate(keep, addin);
  }
};

template<> struct update<resolution::ResolutionResultByPostorderID> {
  bool operator()(resolution::ResolutionResultByPostorderID& keep,
                  resolution::ResolutionResultByPostorderID& addin) const {
    return resolution::ResolutionResultByPostorderID::update(keep, addin);
  }
};

template<> struct update<owned<resolution::ResolvedFunction>> {
  bool operator()(owned<resolution::ResolvedFunction>& keep,
                  owned<resolution::ResolvedFunction>& addin) const {
    // this function is just here to make debugging easier
    return defaultUpdateOwned(keep, addin);
  }
};
/// \endcond

} // end namespace chpl


namespace std {

template<> struct hash<chpl::resolution::CallInfoActual>
{
  size_t operator()(const chpl::resolution::CallInfoActual& key) const {
    return key.hash();
  }
};

template<> struct hash<chpl::resolution::CallInfo>
{
  size_t operator()(const chpl::resolution::CallInfo& key) const {
    return key.hash();
  }
};

template<> struct hash<chpl::resolution::PoiInfo>
{
  size_t operator()(const chpl::resolution::PoiInfo& key) const {
    return key.hash();
  }
};

template<> struct hash<chpl::resolution::TypedFnSignature::WhereClauseResult>
{
  size_t operator()(const chpl::resolution::TypedFnSignature::WhereClauseResult& key) const {
    return key;
  }
};



} // end namespace std


#endif
