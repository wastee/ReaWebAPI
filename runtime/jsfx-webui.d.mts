/// <reference path="./runtime-api.d.ts" />
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
  reserved?: boolean;
  readOnly?: boolean;
}
export type JSFXDispose = () => Promise<void>;
export interface JSFXParamState {
  readonly sessionId: string;
  readonly fxIdent: string;
  readonly count: number;
  readonly parameters: readonly Readonly<JSFXParameter>[];
  readonly revision: string;
  readonly activation: number;
  readonly active: boolean;
}
export interface JSFXParamWriteResult {
  applied: boolean;
  superseded: boolean;
  parameter?: Readonly<JSFXParameter>;
  revision?: string;
}
export interface JSFXParamValue { index: number; normalized: number }
export interface JSFXParams {
  readonly snapshot: JSFXParamState;
  getState(): Promise<JSFXParamState>;
  onChange(listener: (state: JSFXParamState) => void): Promise<JSFXDispose>;
  beginEdit(index: number): Promise<{ gestureId: string }>;
  /** During a gesture, unsent values may be superseded. Discrete writes remain separate. */
  set(index: number, normalized: number): Promise<JSFXParamWriteResult>;
  endEdit(): Promise<JSFXParamState>;
  reset(index: number): Promise<JSFXParamState>;
  beginEditGroup(indices: number[]): Promise<{ gestureId: string }>;
  setMany(values: JSFXParamValue[]): Promise<{ applied: Readonly<JSFXParameter>[]; revision: string }>;
  endEditGroup(): Promise<JSFXParamState>;
}
export interface JSFXCapabilities {
  protocolVersion: 1;
  sessionId: string;
  capabilityRevision: string;
  status: 'parameterOnly' | 'ready' | 'suspended';
  streams: readonly JSFXStreamDeclaration[];
  dataStates: readonly JSFXDataStateDeclaration[];
  commands: readonly JSFXCommandDeclaration[];
  resources: readonly JSFXResourceDeclaration[];
  features: { params: true; paramGroups: true; nativeChannel: boolean; editorSize?: boolean };
  limits: { maxParametersPerGesture: number; [key: string]: number };
}
export interface JSFXDiagnostics {
  sessionId: string; nativeChannel: boolean; status: JSFXCapabilities['status'];
  active: boolean; gestureActive: boolean; presentationMs: number;
  native?: Readonly<Record<string, unknown>>;
  lastError?: string;
}
export type JSFXSchema =
  | { type: 'number' | 'integer'; minimum: number; maximum: number }
  | { type: 'boolean' }
  | { type: 'string'; maxBytes: number }
  | { type: 'array'; minItems?: number; maxItems: number; items: JSFXSchema }
  | { type: 'object'; properties: Record<string, JSFXSchema> };
export interface JSFXStreamDeclaration {
  id: string; kind: 'audio' | 'spectrum' | 'waveform' | 'meter' | 'binary' | 'midi';
  source: string; channels: number; channelLabels?: string[];
  fftSize?: number; points?: number; updateRate?: number;
}
export interface JSFXDataStateDeclaration {
  id: string; schemaVersion: number; schema: JSFXSchema;
  readOnly?: boolean; persistent?: boolean; undo?: boolean;
}
export interface JSFXCommandDeclaration {
  id: string; args: JSFXSchema; result: JSFXSchema;
  cancellable?: boolean; persistent?: boolean; undo?: boolean; maxExecutionMs?: number;
  continuous?: boolean;
}
export interface JSFXResourceDeclaration {
  id: string; format: 'bytes' | 'float32'; maxBytes: number; persistent?: boolean; undo?: boolean;
}
export interface JSFXOperation {
  operationId: string; requestId: string; revision: string;
  status: 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled';
  blockedReason?: string; progress?: number; result?: unknown;
  /** The DSP exceeded maxExecutionMs. Completion still requires its acknowledgement. */
  deadlineExceeded?: boolean;
  error?: { code: string; message: string; details?: unknown };
}
export interface JSFXDataState {
  id: string; revision: string; schemaVersion: number; value: unknown; available: boolean;
}
export interface JSFXDataStateChange {
  sessionId: string; id: string; revision: string; source: string;
}
/** Requires capabilities declared by a cooperating JSFX. */
export interface JSFXDataStateAPI {
  get(id: string): Promise<Readonly<JSFXDataState>>;
  set(id: string, value: unknown, options: { expectedRevision: string; requestId?: string }): Promise<Readonly<JSFXOperation>>;
  onChange(id: string, listener: (event: Readonly<JSFXDataStateChange>) => void): Promise<JSFXDispose>;
}
export interface JSFXResources {
  beginUpload(id: string, options: { format: 'bytes' | 'float32'; totalBytes: number; metadata?: Record<string, unknown>; requestId?: string }): Promise<Readonly<{ transferId: string; maxChunkBytes: number }>>;
  writeChunk(transferId: string, offset: number, bytes: Uint8Array): Promise<Readonly<{ offset: number; acceptedBytes: number }>>;
  commitUpload(transferId: string, options: { expectedRevision: string; checksum: string; requestId?: string }): Promise<Readonly<JSFXOperation>>;
  abortUpload(transferId: string): Promise<Readonly<{ aborted: boolean }>>;
  openDownload(id: string, options?: { revision?: string }): Promise<Readonly<JSFXResourceDownload>>;
  readChunk(transferId: string, offset: number, length: number): Promise<Uint8Array>;
  closeDownload(transferId: string): Promise<Readonly<{ closed: boolean }>>;
}
export interface JSFXResourceDownload {
  transferId: string; revision: string; totalBytes: number; format: 'bytes' | 'float32';
  metadata: Readonly<Record<string, unknown>>; maxChunkBytes: number; checksum: string;
}
export interface JSFXConnection {
  readonly editor: {
    readonly size: Readonly<JSFXEditorSize> | undefined;
    getSize(): Promise<Readonly<JSFXEditorSize>>;
    /** Persist and share content dimensions across instances of this JSFX association. */
    setSize(width: number, height: number): Promise<Readonly<JSFXEditorSize>>;
    onSizeChange(listener: (size: Readonly<JSFXEditorSize>) => void): Promise<JSFXDispose>;
  };
  readonly params: JSFXParams;
  readonly dataState: JSFXDataStateAPI;
  readonly streams: { open(id: string, options?: { updateRate?: number }): Promise<ReaWebStream> };
  readonly commands: {
    invoke(id: string, args: unknown, options?: { requestId?: string }): Promise<Readonly<JSFXOperation>>;
    release(): Promise<Readonly<{ released: boolean }>>;
  };
  readonly operations: {
    get(operationId: string): Promise<Readonly<JSFXOperation>>;
    find(requestId: string): Promise<Readonly<{ operation: JSFXOperation | null }>>;
    cancel(operationId: string): Promise<Readonly<{ accepted: boolean; operation: JSFXOperation }>>;
    onChange(listener: (event: Readonly<JSFXOperation & { sessionId: string }>) => void): Promise<JSFXDispose>;
  };
  readonly resources: JSFXResources;
  readonly active: boolean;
  getCapabilities(): Promise<JSFXCapabilities>;
  getDiagnostics(): Promise<JSFXDiagnostics>;
  present(): Promise<{ presented: boolean }>;
  onCapabilitiesChange(listener: (event: { sessionId: string; capabilityRevision: string; reason: string }) => void): Promise<JSFXDispose>;
  onVisibilityChange(listener: (active: boolean) => void): Promise<JSFXDispose>;
  dispose(): Promise<void>;
}
export interface JSFXEditorSize {
  width: number;
  height: number;
  customized: boolean;
  revision: string;
}
export function connectJSFX(reaper?: {
  lifecycle: { ready: PromiseLike<{ runtime: { jsfx?: { protocolVersion: number } } }> };
  host: { service(name: string): any };
}, onError?: (error: unknown) => void): Promise<JSFXConnection>;
