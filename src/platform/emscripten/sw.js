/* Service worker for the Fruit Ninja web port (PWA offline support).

   Goal: load offline, but prefer the newest deployed build when online.

   Strategy (split by request type):
   - NAVIGATIONS (the HTML entry page): NETWORK-FIRST. Always try the network
     first so a freshly deployed index.html -- and therefore the new
     content-hashed asset URLs it references -- is picked up immediately. On
     network failure (offline) fall back to the cached page. Successful network
     responses refresh the cache so the offline fallback stays current.
   - EVERYTHING ELSE same-origin (content-hashed fruit-ninja-<sha8>.{js,wasm,
     data} + static manifest/favicon/icons): CACHE-FIRST. Hashed names are
     immutable, so once cached they never need revalidation; a new build ships
     new filenames which simply miss the cache and are fetched + stored on
     demand. This keeps the heavy ~48MB .data served from cache (fast/offline)
     while HTML stays fresh.

   sw.js itself is never cached here (the browser runs its own byte-diff update
   check on it). Old builds' hashed assets accumulate under CACHE until it is
   bumped; bump the version suffix to force a clean slate (re-downloads .data). */

var CACHE = 'fruit-ninja-v2';

self.addEventListener('install', function (event) {
  /* Activate the new SW immediately; no precache step. */
  self.skipWaiting();
});

self.addEventListener('activate', function (event) {
  event.waitUntil(
    caches.keys().then(function (names) {
      return Promise.all(names.map(function (name) {
        if (name !== CACHE) return caches.delete(name);
        return undefined;
      }));
    }).then(function () {
      return self.clients.claim();
    })
  );
});

/* Cache a full, successful, same-origin response (skip 206 partials, which the
   Cache API rejects, plus error/opaque responses). Clones before caching. */
function _cachePut(cache, req, resp) {
  if (resp && resp.status === 200 &&
      (resp.type === 'basic' || resp.type === 'default')) {
    cache.put(req, resp.clone());
  }
}

self.addEventListener('fetch', function (event) {
  var req = event.request;

  /* Non-GET or cross-origin: plain passthrough, never cached. */
  if (req.method !== 'GET') return;
  var url;
  try {
    url = new URL(req.url);
  } catch (e) {
    return;
  }
  if (url.origin !== self.location.origin) return;

  /* Never intercept/cache the service worker script itself -- the browser
     manages its own update lifecycle for it. (Matches "/sw.js" and any
     "/<scope>/sw.js".) */
  if (url.pathname.slice(-6) === '/sw.js') {
    return;
  }

  /* NAVIGATION (HTML entry): network-first, prefer the newest build. */
  if (req.mode === 'navigate') {
    event.respondWith(
      caches.open(CACHE).then(function (cache) {
        return fetch(req).then(function (resp) {
          _cachePut(cache, req, resp);
          return resp;
        }).catch(function (err) {
          /* Offline: serve the cached entry page. The first online navigation
             may have been cached under "index.html" or "./" depending on the
             address used, or the request itself. */
          return cache.match('index.html').then(function (idx) {
            return idx || cache.match('./');
          }).then(function (idx) {
            return idx || cache.match(req);
          }).then(function (hit) {
            if (hit) return hit;
            throw err;
          });
        });
      })
    );
    return;
  }

  /* EVERYTHING ELSE: cache-first (content-hashed assets are immutable). */
  event.respondWith(
    caches.open(CACHE).then(function (cache) {
      return cache.match(req).then(function (cached) {
        if (cached) return cached;
        return fetch(req).then(function (resp) {
          _cachePut(cache, req, resp);
          return resp;
        });
      });
    })
  );
});
