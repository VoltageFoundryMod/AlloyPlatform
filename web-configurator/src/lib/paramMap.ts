/**
 * The active module's parameter map.
 *
 * Hand-written, unlike the per-module tables it re-exports — those are emitted
 * by tools/gen_params.py from each module's params.json. This picks between
 * them at build time from VITE_MODULE, so every consumer keeps importing
 * "./lib/paramMap" and none of them has to know more than one module exists.
 *
 * The branch tests `import.meta.env.VITE_MODULE` directly rather than
 * ACTIVE_MODULE.id, because Vite statically replaces the former and the
 * comparison then folds to a constant; a property access on an object is
 * opaque to it.
 *
 * It does **not** tree-shake, though — verified in the built bundle, which
 * carries both modules' tables either way. Rollup keeps a namespace import
 * alive once anything reads through it. That costs a few KB in a ~98 KB
 * bundle for a tool served off localhost, which is not worth restructuring
 * around; the note is here so nobody assumes otherwise and is surprised.
 */

import type { CCParam } from "./paramMapTypes";
import * as alloyflux from "./paramMapAlloyFlux";
import * as alloycoil from "./paramMapAlloyCoil";

export type { CCParam, ParamType, SelectOption } from "./paramMapTypes";
export { ccToFloat, floatToCC } from "./paramMapTypes";

const active = import.meta.env.VITE_MODULE === "alloycoil" ? alloycoil : alloyflux;

// Widened to `string` on purpose. Each generated table narrows its categories
// to a literal union of its own names, and those unions differ between modules
// — so the type of `active` is a union of two unrelated Records, which cannot
// be indexed by either module's category. The categories are a UI grouping
// read out of the data at runtime, not a compile-time contract, so the literal
// types were never buying anything here.
export const PARAM_CATEGORIES: readonly string[] = active.PARAM_CATEGORIES;
export type ParamCategory = string;
export const PARAM_MAP: CCParam[] = active.PARAM_MAP;
export const PARAMS_BY_CATEGORY: Record<string, CCParam[]> =
  active.PARAMS_BY_CATEGORY;
