/**
 * Parameter maps, keyed by module.
 *
 * Hand-written, unlike the per-module tables it re-exports — those are emitted
 * by tools/gen_params.py from each module's params.json. Every module's table
 * is in the bundle and the active one is picked at runtime, from whichever
 * module answered the discovery probe (see activeModule.ts).
 *
 * That costs nothing over the old build-time branch: it never tree-shook
 * either — verified in the built bundle, which carried both modules' tables
 * regardless of VITE_MODULE, because Rollup keeps a namespace import alive once
 * anything reads through it. A few KB in a ~98 KB bundle.
 *
 * Reactive consumers subscribe to `params`; parsers and other non-reactive
 * callers take `currentParams()`.
 */

import { derived, type Readable } from "svelte/store";
import { activeModule, currentModule } from "./activeModule";
import type { CCParam } from "./paramMapTypes";
import * as alloyflux from "./paramMapAlloyFlux";
import * as alloycoil from "./paramMapAlloyCoil";

export type { CCParam, ParamType, SelectOption } from "./paramMapTypes";
export { ccToFloat, floatToCC } from "./paramMapTypes";

/**
 * One module's tables.
 *
 * Categories are widened to `string` on purpose. Each generated table narrows
 * its own to a literal union of its own names, and those unions differ between
 * modules — the categories are a UI grouping read out of the data at runtime,
 * not a compile-time contract, so the literal types were never buying anything
 * here.
 */
export interface ModuleParams {
  readonly PARAM_CATEGORIES: readonly string[];
  readonly PARAM_MAP: CCParam[];
  readonly PARAMS_BY_CATEGORY: Record<string, CCParam[]>;
}

export type ParamCategory = string;

const TABLES: Readonly<Record<string, ModuleParams>> = {
  alloyflux,
  alloycoil,
};

/** Tables for a module id. Falls back to AlloyFlux rather than throwing. */
export function paramsFor(moduleId: string): ModuleParams {
  return TABLES[moduleId] ?? TABLES.alloyflux;
}

/** Tables for the active module, for code that cannot subscribe. */
export function currentParams(): ModuleParams {
  return paramsFor(currentModule().id);
}

/** Tables for the active module, following it as it changes. */
export const params: Readable<ModuleParams> = derived(activeModule, (m) =>
  paramsFor(m.id),
);
