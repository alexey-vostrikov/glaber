const path = require("path");
const webpack = require("webpack");
const TerserPlugin = require('terser-webpack-plugin');

module.exports = {
    entry: "./src/index.js",
    mode: "production",
    module: {
        rules: [
            {
                test: /\.(js|jsx)$/,
                exclude: /(node_modules|bower_components)/,
                loader: "babel-loader",
                options: { presets: ["@babel/env"] }
            }
        ]
    },
    resolve: { extensions: ["*", ".js", ".jsx"] },
    output: {
        path: path.resolve(__dirname, "../ui/js"),
        publicPath: "/",
        filename: 'react.components.js'
    }
//    ,
    // optimization: {
    //     minimize: true,
    //     minimizer: [
    //         new TerserPlugin({
    //             extractComments: false,
    //             // terserOptions: {
    //             //     format: {
    //             //         comments: false,
    //             //     },
    //             // },
    //         }),
    //     ],
    // },
};