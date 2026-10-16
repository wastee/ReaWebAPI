export interface JSFXParameter {
  index: number;
  name: string | null;
  ident: string | null;
  value: number;
  normalized: number;
  min: number;
  max: number;
  mid: number;
  defaultNormalized: number | null;
  section: string | null;
  step: number | null;
  smallStep: number | null;
  largeStep: number | null;
  toggle: boolean | null;
  enum: null;
  formatted: string | null;
}
export interface JSFXState { count: number; parameters: JSFXParameter[]; fxIdent: string; revision: number; activation: number; active: boolean }
export interface JSFXConnection {
  readonly state: JSFXState;
  readonly active: boolean;
  getState(): Promise<JSFXState>;
  present(): Promise<void>;
  onChange(listener: (state: JSFXState) => void): () => void;
  onVisibilityChange(listener: (active: boolean) => void): () => void;
  beginEdit(index: number): Promise<void>;
  /** Keeps only the latest unsent normalized value per parameter. */
  setParam(index: number, normalized: number): void;
  endEdit(): Promise<JSFXState>;
  reset(index: number): Promise<JSFXState>;
  dispose(): Promise<void>;
}
export function connectJSFX(reaper?: { host: { service(name: string): any } }, onError?: (error: unknown) => void): Promise<JSFXConnection>;
