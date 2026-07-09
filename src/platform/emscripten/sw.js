/* Service worker for the Fruit Ninja web port (PWA offline support).
   Hash-agnostic runtime caching: the game's asset filenames are content-hashed
   (fruit-ninja-<sha8>.{js,wasm,data}, splash-<sha8>.webp) and change per build,
   so there is NO precache list.  Instead, every successful same-origin GET is
   cached as it is fetched (cache-first thereafter), so after one full online
   load (including the ~48MB .data bundle) the game runs offline.
   Old builds' assets accumulate under the same cache name until CACHE is
   bumped; bump the version suffix to force a clean slate. */

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

  event.respondWith(
    caches.open(CACHE).then(function (cache) {
      return cache.match(req).then(function (cached) {
        if (cached) return cached;
        return fetch(req).then(function (resp) {
          /* Cache only full, successful responses (skip 206 partials, which
             the Cache API rejects, and error/opaque responses). */
          if (resp && resp.status === 200 &&
              (resp.type === 'basic' || resp.type === 'default')) {
            cache.put(req, resp.clone());
          }
          return resp;
        }).catch(function (err) {
          /* Offline navigation fallback: serve the cached entry page.
             The first online navigation may have been cached under "/" or
             "index.html" depending on the address used -- try both. */
          if (req.mode === 'navigate') {
            return cache.match('index.html').then(function (idx) {
              return idx || cache.match('./');
            }).then(function (idx) {
              if (idx) return idx;
              throw err;
            });
          }
          throw err;
        });
      });
    })
  );
});
