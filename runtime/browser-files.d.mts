export const MAX_DOWNLOAD_BYTES: number;
interface FileHost {
  dialog: { saveFile(options: { title: string; initialPath: string }): Promise<string | null> };
  fs: { writeBinary(path: string, bytes: Uint8Array, options: { overwrite: boolean }): Promise<unknown> };
}
/** Returns the selected path, or null on cancellation. Rejects exports over 16 MiB. */
export function saveBlob(blob: Blob, filename: string, host?: FileHost): Promise<string | null>;
export function installDownloadHandler(options?: {
  onError?: (error: unknown) => void;
  host?: FileHost;
  document?: Document;
}): () => void;
