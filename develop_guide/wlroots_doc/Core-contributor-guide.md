Welcome to the project! Please read this scatterbrained collection of wisdom gathered by other core contributors.

## Golden rules

- Breaking the rules is okay, but only if you are prepared to justify it to everyone else (and they agree with you).
- Edit this wiki page

## Submitting your patches

Avoid merging or directly committing your own patches, unless they're small and limited to documentation or something. For most changes, submit the patch normally.

## Triaging patches

If you see a patch which doesn't have any reviewers assigned (including ones you submit yourself): triage it. Add some labels if you like, and assign some reviewers. Follow these steps until you have a number of reviewers proportional to the complexity of the change:

1. Assign people who work a lot in the domain of the patch
2. Assign a selection from everyone else with a roughly even distribution

If you get assigned to a patch you don't think is in your domain and/or you don't have time to review, assign someone else.

## Reviewing patches

- Don't be too shy to complain about code style issues
- Ask for a test plan if you don't know how to test it

## Merging patches

Wait for the continuous integration build to complete before merging.

- If it's small-to-medium and in your domain, merge it whenever you feel comfortable with it
- If it's medium-to-large, merge it when it has 2 approving reviews
- If it's large-to-huge, Simon will merge it

Generally just use your common sense here.

Don't forget to thank the contributor.

## Releasing a new version

1. Remove the `-dev` suffix in `meson.build` and commit the changes
2. Create an annotated tag and push it: `git tag $version -ase` and then `git push --follow-tags`
3. Create an archive: `git archive --prefix="wlroots-$version/" -o wlroots-$version.tar.gz $version`
4. Sign the archive: `gpg --detach-sign wlroots-$version.tar.gz`
5. Create the release on GitLab
6. Bump the project version in `meson.build` for the next release, re-add the `-dev` suffix
7. Bump the SONAME in `meson.build` for the next release
8. Commit and push the changes