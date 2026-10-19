# Hosting sci4this.com

The Pages workflow publishes the committed root `index.html` at `/`.
It stages only `index.html` and `.nojekyll` under `build/pages`;
it does not compile, initialize submodules, or install guest files. The root URL
serves the self-contained document directly, allowing early loading markup to
render while the embedded payload downloads.

The previous domain forwarding used a full-page frame pointing at
htmlpreview.github.io. That service downloads the complete document with
`Response.text()` before inserting it. Loading scripts inside Bash cannot run
during that download. Do not use that preview URL for production hosting.

## First deployment

1. Copy the validated build with `cp build/html-rt/bash.html ./index.html`,
   then commit and push `index.html` and `.github/workflows/pages.yml` to `main`.
   Include removal of the old root `bash.html` in the initial migration commit.
2. In [repository Pages settings](https://github.com/SCI4THIS/waste/settings/pages),
   select **GitHub Actions** as the build/deployment source.
3. In **Actions → Deploy Bash to GitHub Pages**, run the workflow on `main`
   (or rerun its initial run if Pages was not enabled yet). Wait for success.
4. Before configuring the custom domain, verify
   `https://sci4this.github.io/waste/` opens Bash directly.
5. In Pages settings, set **Custom domain** to `sci4this.com` and save it
   before changing DNS. This Actions deployment does not require a `CNAME`
   file; the custom-domain setting is managed in GitHub.

## Domain provider settings

Disable URL forwarding, masked forwarding, and parking for `sci4this.com`.
Replace the web-hosting records for the apex (`@`) with:

| Type | Name | Value |
| --- | --- | --- |
| A | @ | 185.199.108.153 |
| A | @ | 185.199.109.153 |
| A | @ | 185.199.110.153 |
| A | @ | 185.199.111.153 |
| CNAME | www | sci4this.github.io |

Use the provider's default TTL, or 300 seconds during migration. Replace any
conflicting `www` records. Remove old apex AAAA records pointing to the forwarding
service; optionally replace them with all four GitHub Pages IPv6 addresses:
`2606:50c0:8000::153`, `2606:50c0:8001::153`, `2606:50c0:8002::153`, and
`2606:50c0:8003::153`. Keep unrelated MX and TXT records, including email records.
DNS targets contain no `https://`, repository name, or URL path.

After GitHub's DNS check succeeds and its certificate is available, enable
**Enforce HTTPS** in Pages settings. DNS propagation and certificate availability
can take up to 24 hours. Check `https://sci4this.com/` and
`https://sci4this.com/index.html`; `www` should redirect to the apex domain.
The browser's Network panel should show a document served from sci4this.com,
without requests to htmlpreview.github.io or raw.githubusercontent.com.

## Publishing updates

Build and validate `build/html-rt/bash.html` using the normal browser workflow.
Publish the reviewed output from the repository root:

```sh
cp build/html-rt/bash.html ./index.html
git add index.html
git commit -m "Update published page"
git push origin main
```

Changes to root `index.html` on `main` trigger deployment. Source-only changes do not rebuild
or replace the published snapshot automatically. A manual workflow run republishes
the currently committed snapshot.

Reference: [GitHub custom-domain and DNS instructions](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site)
and [GitHub's static Pages workflow](https://github.com/actions/starter-workflows/blob/main/pages/static.yml).
