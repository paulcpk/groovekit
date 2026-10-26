# Contributing

Thanks for helping with GrooveKit.

## Licensing of contributions

GrooveKit is released under the GNU Affero General Public License v3. The maintainer may also offer it under other terms (for example a commercial license), so every contribution needs to be licensable that way too.

By opening a pull request you confirm that:

1. You wrote the contribution, or have the right to submit it.
2. You license it to the public under the AGPLv3, like the rest of GrooveKit.
3. You also grant the maintainer (Paul Csapak) a perpetual, worldwide, non-exclusive, royalty-free, irrevocable license to use, modify and distribute it, and to sublicense it under other terms, including proprietary ones.

You keep the copyright in your contribution. Pull requests may be asked to sign a contributor license agreement with the same effect before they're merged.

## Working on the code

See the README for building and running. Keep commits focused, and run the tests before opening a pull request:

```sh
cmake --build --preset dev && ctest --preset dev
pnpm test
```
