const path = require('path');
const webpack = require('webpack');

const root = path.resolve(__dirname, '..');
const compiler = webpack({
  mode: 'production',
  target: 'web',
  entry: path.join(__dirname, 'pairing_crypto_entry.js'),
  output: {
    path: path.join(root, 'vita', 'assets'),
    filename: 'pairing-crypto.js',
    library: {
      name: 'MuseVitaCrypto',
      type: 'assign',
    },
  },
  optimization: {
    minimize: true,
  },
});

compiler.run((error, stats) => {
  if (error) throw error;
  if (stats.hasErrors()) {
    console.error(stats.toString({ all: false, errors: true }));
    process.exitCode = 1;
    return;
  }
  console.log(stats.toString({ all: false, assets: true, builtAt: true }));
  compiler.close(closeError => {
    if (closeError) {
      console.error(closeError);
      process.exitCode = 1;
    }
  });
});
