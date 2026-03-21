/**
 * Profanity filter service — censors offensive words with asterisks.
 * Keeps first and last letter, replaces middle with '*'.
 * Supports Spanish and English profanity.
 */

const PROFANITY_LIST = [
  // ── English ──
  'fuck', 'fucker', 'fucking', 'fucked', 'motherfucker',
  'shit', 'shitty', 'bullshit', 'shitting',
  'bitch', 'bitches', 'bitching',
  'asshole', 'arsehole',
  'bastard', 'bastards',
  'damn', 'damned', 'dammit',
  'dick', 'dickhead',
  'cunt', 'cunts',
  'whore', 'whores',
  'slut', 'sluts',
  'piss', 'pissed',
  'cock', 'cocksucker',
  'nigger', 'nigga', 'niggers', 'niggas',
  'retard', 'retarded',
  'faggot', 'fag', 'faggots',
  'twat',
  'wanker',
  'crap', 'crappy',

  // ── Spanish ──
  'puta', 'putas', 'putita', 'putitas',
  'puto', 'putos', 'putito', 'putazo',
  'mierda', 'mierdas', 'mierdero',
  'pendejo', 'pendejos', 'pendeja', 'pendejas',
  'cabron', 'cabrón', 'cabrones', 'cabrona',
  'chingar', 'chingada', 'chingado', 'chingados', 'chingadera',
  'verga', 'vergudo', 'vergota',
  'culero', 'culera', 'culeros',
  'culo',
  'joder', 'jodido', 'jodida', 'jodete',
  'marica', 'maricon', 'maricón', 'mariconada',
  'mamón', 'mamon', 'mamonazo',
  'huevon', 'huevón', 'güey',
  'pinche', 'pinches',
  'coño', 'cono',
  'zorra', 'zorras',
  'idiota', 'idiotas',
  'imbecil', 'imbécil', 'imbeciles',
  'estupido', 'estúpido', 'estupida', 'estúpida',
  'malparido', 'malparida',
  'hijueputa', 'hijodeputa', 'hijaputa',
  'gonorrea',
  'baboso', 'babosa',
  'tarado', 'tarada',
  'boludo', 'boluda',
  'pelotudo', 'pelotuda',
  'forro', 'forra',
  'negro de mierda', 'negra de mierda',
  'subnormal',
];

// Build a Set of lowercase words for fast lookup
const WORD_SET = new Set(PROFANITY_LIST.map(w => w.toLowerCase()));

// Build regex: match whole words (with accented char awareness), case-insensitive
// Multi-word phrases first (sorted by length desc), then single words
const sorted = [...PROFANITY_LIST].sort((a, b) => b.length - a.length);
const escaped = sorted.map(w => w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
const PROFANITY_REGEX = new RegExp(`\\b(${escaped.join('|')})\\b`, 'gi');

/**
 * Censor a word: keep first/last char, replace middle with '*'.
 * Words ≤ 2 chars are fully replaced with '*'.
 */
function censorWord(word) {
  if (word.length <= 2) return '*'.repeat(word.length);
  return word[0] + '*'.repeat(word.length - 2) + word[word.length - 1];
}

/**
 * Censor profanity in text. Replaces middle characters with asterisks.
 * @param {string} text - Input text
 * @returns {string} Censored text
 */
export function censorText(text) {
  if (!text || typeof text !== 'string') return text || '';
  return text.replace(PROFANITY_REGEX, (match) => censorWord(match));
}

/**
 * Check if text contains profanity (without censoring).
 * @param {string} text
 * @returns {boolean}
 */
export function containsProfanity(text) {
  if (!text || typeof text !== 'string') return false;
  return PROFANITY_REGEX.test(text);
}
