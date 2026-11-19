"use strict";
// Website login: the password and a secret live in <data>/web.txt, shared with pap-core
// (the display shows the password, Settings -> Network). The cookie holds
// sha256(secret + ":" + password): a new password logs every browser out.
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.webFile = webFile;
exports.readLogin = readLogin;
exports.setPassword = setPassword;
exports.token = token;
exports.cookieToken = cookieToken;
const node_crypto_1 = require("node:crypto");
const fs = __importStar(require("node:fs"));
const path = __importStar(require("node:path"));
function webFile(dataDir) {
    return path.join(dataDir, 'web.txt');
}
function write(file, l) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const tmp = file + '.tmp';
    fs.writeFileSync(tmp, `# website login (pap-web reads this file)\npassword = ${l.password}\nsecret = ${l.secret}\n`);
    fs.renameSync(tmp, file);
}
function randomLetters(n) {
    return Array.from((0, node_crypto_1.randomBytes)(n), (b) => String.fromCharCode(97 + (b % 26))).join('');
}
// Read (and create when missing) the login file
function readLogin(dataDir) {
    const file = webFile(dataDir);
    const l = { password: '', secret: '' };
    try {
        for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
            const eq = line.indexOf('=');
            if (eq < 0 || line.trimStart().startsWith('#'))
                continue;
            const k = line.slice(0, eq).trim(), v = line.slice(eq + 1).trim();
            if (k === 'password')
                l.password = v;
            else if (k === 'secret')
                l.secret = v;
        }
    }
    catch {
        /* created below */
    }
    if (!l.password || !l.secret) {
        if (!l.password)
            l.password = randomLetters(8);
        if (!l.secret)
            l.secret = (0, node_crypto_1.randomBytes)(16).toString('hex');
        write(file, l);
    }
    return l;
}
function setPassword(dataDir, password) {
    write(webFile(dataDir), { password, secret: (0, node_crypto_1.randomBytes)(16).toString('hex') });
}
function token(l) {
    return (0, node_crypto_1.createHash)('sha256').update(l.secret + ':' + l.password).digest('hex');
}
function cookieToken(cookieHeader) {
    for (const part of (cookieHeader || '').split(';')) {
        const [k, ...v] = part.trim().split('=');
        if (k === 'pap')
            return v.join('=');
    }
    return '';
}
//# sourceMappingURL=auth.js.map