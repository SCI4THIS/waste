import {parentPort} from "node:worker_threads";
import {installWasteWorker} from "../command/waste.worker.mjs";
installWasteWorker(parentPort);
