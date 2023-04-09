const path = require("path");
const dst = path.resolve(__dirname, "../js");
const TerserPlugin = require('terser-webpack-plugin');

module.exports = {
    entry: "./src/index.ts",
    mode: "production",
    module: {
        rules: [
            {
                test: /\.tsx?$/,
                loader: "ts-loader",
                exclude: /node_modules/,
                options: {
                    configFile: "tsconfig.json"
                }
            }
        ]
    },
    resolve: {
        extensions: [".tsx", ".ts", ".js"]
    },
    output: {
        filename: "glaber.components.js",
        path: dst
    },
    plugins: [],
    optimization: {
        minimize: true,
        minimizer: [
            new TerserPlugin({
                extractComments: false,
                terserOptions: {
                    format: {
                        comments: false,
                    },
                },
            }),
        ],
    },
};